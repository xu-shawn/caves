// The table gate of the cave search on a GPU, as a first look: for each seed the two gate numbers on a grid of every
// second window per axis (windows 256 blocks apart): the 512-block window-mean sum at pair 11,544 and the 1280-block
// one at pair 62 (a sixteenth of the pairs of the full grid). Their score follows the full one with r = 0.983, so the
// host keeps a few percent and redoes those exactly (analyticgate.hpp, which this mirrors: same weights, same sums).
// One block of 256 threads per seed, the thread being the column (z, or the window along z), so a warp reads and
// writes neighbours. The host has the permutation tables and every weight ready (TableLayer). Per layer the block
// walks the windows along x: the gradient sums down y of a lattice line (x, every z) go into a ring of lines in
// shared memory, each computed once; a window's row along x is a weighted sum of its lines, also in shared memory;
// the windows along z of that row are weighted sums of it and go to the field. Only the fields touch global memory
// (the first version kept the gradient sums and the rows there and was bound by memory traffic: 9,000 seeds a second;
// gputables_v1.cu). Then the 4 x 4 window means and the two pair counts by histogram, as the CPU code does them,
// except that the histograms start at a fixed value (-0.375 per stack, 2,048 bins of 1/4096) instead of each field's
// minimum, so that they fit in shared memory and fill while the window means are made: the numbers differ from the
// CPU's by a fraction of a bin (the version with the CPU's exact bins: gputables_v2.cu). A seed whose 512-block number
// is above a level set by the host stops there (gputables_skip): 7 in 10 seeds, a fifth of the kernel's work.
// build: nvcc -O3 -c gputables.cu -o gputables.o
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cuda_runtime.h>
#include "gpuscan.h"

static const int PLANE = 256 * 256, HBINS = 2048, PER_BLOCK = 4 * PLANE, RING = 6;
static const float ORIGIN = -0.375f;

// the smallest window-mean sum a + b that at least `pairs` pairs stay under, from the two histograms (shared memory)
__device__ float pairQuantile(uint64_t pairs, unsigned int *histA, unsigned int *histB, unsigned long long *part, int *range) {
    const int t = threadIdx.x;
    __syncthreads();
    if (t == 0) { for (int i = 1; i < HBINS; i++) histB[i] += histB[i - 1]; range[0] = 0; range[1] = 2 * HBINS - 1; }
    __syncthreads();
    for (int step = 0; step < 12; step++) { // 4096 sums of two bins: 12 halvings
        const int mid = (range[0] + range[1]) / 2; unsigned long long sum = 0;
        for (int i = t; i <= mid && i < HBINS; i += 256) { const int j = mid - i; sum += (unsigned long long) histA[i] * histB[j < HBINS ? j : HBINS - 1]; }
        part[t] = sum;
        __syncthreads();
        if (t == 0) { unsigned long long total = 0; for (int i = 0; i < 256; i++) total += part[i]; if (total >= pairs) range[1] = mid; else range[0] = mid + 1; }
        __syncthreads();
    }
    return 2 * ORIGIN + (range[0] + 1) / 4096.0f;
}
__device__ __forceinline__ void count(unsigned int *hist, float v) { int bin = (int) ((v - ORIGIN) * 4096.0f); if (bin < 0) bin = 0; if (bin < HBINS) atomicAdd(&hist[bin], 1u); }

__global__ void tables(const TableSeed *in, float *scratch, float *out, float skip) {
    static const float GX[16] = {1, -1, 1, -1, 1, -1, 1, -1, 0, 0, 0, 0, 1, 0, -1, 0}, GY[16] = {1, 1, -1, -1, 0, 0, 0, 0, 1, -1, 1, -1, 1, -1, 1, -1}, GZ[16] = {0, 0, 0, 0, 1, 1, -1, -1, 1, 1, -1, -1, 0, 1, 0, -1};
    __shared__ unsigned char d[256]; __shared__ float ring[3 * RING * 256], rowXY[256], rowZ[256]; __shared__ unsigned long long part[256]; __shared__ int range[2];
    const int t = threadIdx.x; const TableSeed &seed = in[blockIdx.x]; float *base = scratch + (size_t) blockIdx.x * PER_BLOCK;
    float *field[2] = {base, base + PLANE}, *rows[2] = {base + 2 * PLANE, base + 3 * PLANE};
    float *ringX = ring, *ringY = ring + RING * 256, *ringZ = ring + 2 * RING * 256; unsigned int *histA = (unsigned int *) ring, *histB = histA + HBINS; // the histograms use the ring's memory once the layers are done
    for (int li = 0; li < 4; li++) { // octave -8 A, -7 A, -8 B, -7 B
        const TableLayer &L = seed.layer[li]; const int cells = (li & 1) ? 2 : 1, taps = 2 * cells + 2, period = 256 / cells, planes = L.planes, y0 = L.y0, baseX = L.baseX; float *f = field[li >> 1];
        float wa[4], wb[4], kxa[6], kxb[6], kza[6], kzb[6]; int zs[6]; const float norm = L.norm;
        for (int k = 0; k < planes; k++) { wa[k] = L.wyA[k]; wb[k] = L.wyB[k]; }
        for (int r = 0; r < taps; r++) { kxa[r] = L.kxA[r]; kxb[r] = L.kxB[r]; kza[r] = L.kzA[r]; kzb[r] = L.kzB[r]; zs[r] = (L.baseZ + t * cells + r) & 255; }
        d[t] = L.d[t];
        __syncthreads();
        int have = 0; // lattice lines baseX .. baseX + have - 1 are in the ring (line i in slot i mod RING)
        for (int m = 0; m < period; m++) {
            for (; have < m * cells + taps; have++) { // the gradient sums down y of the columns (x, z = t) of a new line
                const unsigned char dx = d[(baseX + have) & 255]; float sx = 0, sy = 0, sz = 0; const int slot = (have % RING) * 256 + t;
                for (int k = 0; k < planes; k++) { const int h = d[(unsigned char) (d[(unsigned char) (dx + (unsigned char) (y0 + k))] + t)] & 15; sx += GX[h] * wa[k]; sy += GY[h] * wb[k]; sz += GZ[h] * wa[k]; }
                ringX[slot] = sx; ringY[slot] = sy; ringZ[slot] = sz;
            }
            { // along x: the window that starts at line baseX + m x cells, for the corner line z = t
                float a = 0, c = 0;
                for (int r = 0; r < taps; r++) { const int slot = ((m * cells + r) % RING) * 256 + t; a += kxb[r] * ringX[slot] + kxa[r] * ringY[slot]; c += kxa[r] * ringZ[slot]; }
                rowXY[t] = a; rowZ[t] = c;
            }
            __syncthreads();
            if (t < period) { // along z: window n = t of this row, into every copy of it (the layer repeats after `period` windows)
                float sum = 0;
                for (int r = 0; r < taps; r++) sum += kza[r] * rowXY[zs[r]] + kzb[r] * rowZ[zs[r]];
                sum *= norm;
                if (cells == 1) f[m * 256 + t] = sum; // octave -8 comes first and fills the field
                else for (int i = m; i < 256; i += period) for (int k = t; k < 256; k += period) f[i * 256 + k] += sum;
            }
            __syncthreads();
        }
    }
    // the 512-block windows into the histograms, and their sums of 4 along the rows
    for (int i = t; i < 2 * HBINS; i += 256) histA[i] = 0;
    __syncthreads();
    for (int half = 0; half < 2; half++) {
        const float *f = field[half]; float *w = rows[half]; unsigned int *hist = half ? histB : histA;
        for (int i = 0; i < 256; i++) { const float v = f[i * 256 + t]; count(hist, v); w[i * 256 + t] = v + f[i * 256 + ((t + 1) & 255)] + f[i * 256 + ((t + 2) & 255)] + f[i * 256 + ((t + 3) & 255)]; }
    }
    const float narrow = pairQuantile(11544, histA, histB, part, range);
    if (narrow > skip) { if (t == 0) { out[2 * blockIdx.x] = narrow; out[2 * blockIdx.x + 1] = 1.0f; } return; } // no seed that passes has a 512-block number this high: the 1280-block one is not made (the same for every thread of the block)
    for (int i = t; i < 2 * HBINS; i += 256) histA[i] = 0;
    __syncthreads();
    for (int half = 0; half < 2; half++) { // the 1280-block windows (4 x 4 of the 512-block ones): sums of 4 of those along the columns, straight into the histograms
        const float *w = rows[half]; unsigned int *hist = half ? histB : histA;
        for (int i = 0; i < 256; i++) count(hist, (w[i * 256 + t] + w[((i + 1) & 255) * 256 + t] + w[((i + 2) & 255) * 256 + t] + w[((i + 3) & 255) * 256 + t]) * 0.0625f);
    }
    const float broad = pairQuantile(62, histA, histB, part, range);
    if (t == 0) { out[2 * blockIdx.x] = narrow; out[2 * blockIdx.x + 1] = broad; }
}

// Version 4 of the kernel (tables() above is version 3, kept for GPUTABLES_OLD=1): the layers with the z kernel made once.
// Along z every lattice line of a layer reads the same ring of 256 gradients, turned by one byte per line and height
// (see cpuprelook2.hpp), so the windows' kernel along z is applied once to that ring, per height with its y weight:
//     tableP[k][j] = wyA[k] sum_r kzA[r] GX[d[j + r]],   tableQ[k][j] = sum_r (wyB[k] kzA[r] GY + wyA[k] kzB[r] GZ)[d[j + r]]
// A thread is now a window column: it walks the lattice lines along x with its own ring of lines (already summed over
// its window along z: `planes` reads of each table), and a window is the kernel along x on them. No thread waits for
// another inside a layer; version 3 made every line on 256 lattice columns, filtered every row along z from shared
// memory and met twice per row. The fields are the same sums in another order; the rest of the kernel is unchanged.
static const int LINES4 = 264;
__global__ void tables4(const TableSeed *in, float *scratch, float *out, float skip) {
    static const float GX[16] = {1, -1, 1, -1, 1, -1, 1, -1, 0, 0, 0, 0, 1, 0, -1, 0}, GY[16] = {1, 1, -1, -1, 0, 0, 0, 0, 1, -1, 1, -1, 1, -1, 1, -1}, GZ[16] = {0, 0, 0, 0, 1, 1, -1, -1, 1, 1, -1, -1, 0, 1, 0, -1};
    __shared__ float ring[3 * RING * 256]; __shared__ unsigned char turn[4 * LINES4]; __shared__ unsigned long long part[256]; __shared__ int range[2];
    const int t = threadIdx.x; const TableSeed &seed = in[blockIdx.x]; float *base = scratch + (size_t) blockIdx.x * PER_BLOCK;
    float *field[2] = {base, base + PLANE}, *rows[2] = {base + 2 * PLANE, base + 3 * PLANE};
    float *tableP = ring, *tableQ = ring + 4 * 256; unsigned int *histA = (unsigned int *) ring, *histB = histA + HBINS; // the tables, then the histograms, in the same shared memory
    for (int li = 0; li < 4; li++) { // octave -8 A, -7 A, -8 B, -7 B
        const TableLayer &L = seed.layer[li]; const int cells = (li & 1) ? 2 : 1, taps = 2 * cells + 2, period = 256 / cells, planes = L.planes, lines = (period - 1) * cells + taps; float *f = field[li >> 1];
        float kxa[6], kxb[6]; const float norm = L.norm;
        for (int r = 0; r < taps; r++) { kxa[r] = L.kxA[r]; kxb[r] = L.kxB[r]; }
        { // this thread's entry of the tables (j = t) and its share of the lines' turns
            float ax = 0, ay = 0, az = 0;
            for (int r = 0; r < taps; r++) { const int h = L.d[(t + r) & 255] & 15; ax += L.kzA[r] * GX[h]; ay += L.kzA[r] * GY[h]; az += L.kzB[r] * GZ[h]; }
            for (int k = 0; k < planes; k++) { tableP[k * 256 + t] = L.wyA[k] * ax; tableQ[k * 256 + t] = L.wyB[k] * ay + L.wyA[k] * az; }
            for (int line = t; line < lines; line += 256) { const unsigned char dx = L.d[(L.baseX + line) & 255]; for (int k = 0; k < planes; k++) turn[k * LINES4 + line] = (unsigned char) (L.d[(unsigned char) (dx + (unsigned char) (L.y0 + k))] + L.baseZ); }
        }
        __syncthreads();
        if (t < period) { // window column t: its windows start at lattice column baseZ + t x cells
            const int c0 = t * cells; float lineP[8], lineQ[8]; int have = 0; // lattice lines 0 .. have - 1 are made (line i in slot i & 7)
            for (int m = 0; m < period; m++) {
                for (; have < m * cells + taps; have++) {
                    float p = 0, q = 0;
                    for (int k = 0; k < planes; k++) { const int j = (turn[k * LINES4 + have] + c0) & 255; p += tableP[k * 256 + j]; q += tableQ[k * 256 + j]; }
                    lineP[have & 7] = p; lineQ[have & 7] = q;
                }
                float sum = 0;
                for (int r = 0; r < taps; r++) { const int slot = (m * cells + r) & 7; sum += kxb[r] * lineP[slot] + kxa[r] * lineQ[slot]; }
                sum *= norm;
                if (cells == 1) f[m * 256 + t] = sum; // octave -8 comes first and fills the field
                else for (int i = m; i < 256; i += period) for (int k = t; k < 256; k += period) f[i * 256 + k] += sum;
            }
        }
        __syncthreads();
    }
    // the 512-block windows into the histograms, and their sums of 4 along the rows
    for (int i = t; i < 2 * HBINS; i += 256) histA[i] = 0;
    __syncthreads();
    for (int half = 0; half < 2; half++) {
        const float *f = field[half]; float *w = rows[half]; unsigned int *hist = half ? histB : histA;
        for (int i = 0; i < 256; i++) { const float v = f[i * 256 + t]; count(hist, v); w[i * 256 + t] = v + f[i * 256 + ((t + 1) & 255)] + f[i * 256 + ((t + 2) & 255)] + f[i * 256 + ((t + 3) & 255)]; }
    }
    const float narrow = pairQuantile(11544, histA, histB, part, range);
    if (narrow > skip) { if (t == 0) { out[2 * blockIdx.x] = narrow; out[2 * blockIdx.x + 1] = 1.0f; } return; } // no seed that passes has a 512-block number this high: the 1280-block one is not made (the same for every thread of the block)
    for (int i = t; i < 2 * HBINS; i += 256) histA[i] = 0;
    __syncthreads();
    for (int half = 0; half < 2; half++) { // the 1280-block windows (4 x 4 of the 512-block ones): sums of 4 of those along the columns, straight into the histograms
        const float *w = rows[half]; unsigned int *hist = half ? histB : histA;
        for (int i = 0; i < 256; i++) count(hist, (w[i * 256 + t] + w[((i + 1) & 255) * 256 + t] + w[((i + 2) & 255) * 256 + t] + w[((i + 3) & 255) * 256 + t]) * 0.0625f);
    }
    const float broad = pairQuantile(62, histA, histB, part, range);
    if (t == 0) { out[2 * blockIdx.x] = narrow; out[2 * blockIdx.x + 1] = broad; }
}

// A cheaper look before that one: octave -8 alone (the layer that carries most of a wide window's mean), 1280-block
// windows only every 512 blocks, and of each stack just the lowest window; the two lowest added. A seed whose sum is not
// low cannot have a low 1280-block pair number: a cut that passes a fifth of the seeds keeps 99.8% of the best 400 of
// 120,000 printed ones. A quarter of the work of tables(): half the layers, a quarter of the windows, no histograms.
static const int RING1 = 8;
__global__ void prelook(const TableSeed *in, float *out) {
    static const float GX[16] = {1, -1, 1, -1, 1, -1, 1, -1, 0, 0, 0, 0, 1, 0, -1, 0}, GY[16] = {1, 1, -1, -1, 0, 0, 0, 0, 1, -1, 1, -1, 1, -1, 1, -1}, GZ[16] = {0, 0, 0, 0, 1, 1, -1, -1, 1, 1, -1, -1, 0, 1, 0, -1};
    __shared__ unsigned char d[256]; __shared__ float ring[3 * RING1 * 256], rowXY[256], rowZ[256], mins[128];
    const int t = threadIdx.x; const TableSeed &seed = in[blockIdx.x]; float total = 0;
    float *ringX = ring, *ringY = ring + RING1 * 256, *ringZ = ring + 2 * RING1 * 256;
    for (int stack = 0; stack < 2; stack++) {
        const TableLayer &L = seed.layer[2 * stack]; const int planes = L.planes, y0 = L.y0, baseX = L.baseX; // octave -8 of the stack
        float wa[4], wb[4], kxa[7], kxb[7], kza[7], kzb[7]; int zs[7]; const float norm = L.norm * 0.0625f;
        for (int k = 0; k < planes; k++) { wa[k] = L.wyA[k]; wb[k] = L.wyB[k]; }
        for (int r = 0; r < 7; r++) kxa[r] = kxb[r] = kza[r] = kzb[r] = 0; // the tapered window: four 512-block windows a cell apart
        for (int j = 0; j < 4; j++) for (int r = 0; r < 4; r++) { kxa[r + j] += L.kxA[r]; kxb[r + j] += L.kxB[r]; kza[r + j] += L.kzA[r]; kzb[r + j] += L.kzB[r]; }
        for (int r = 0; r < 7; r++) zs[r] = (L.baseZ + 2 * t + r) & 255; // window column t (< 128) starts at cell 2 t
        d[t] = L.d[t];
        __syncthreads();
        int have = 0; float lowest = 1e9f;
        for (int m = 0; m < 128; m++) { // window row m starts at lattice line baseX + 2 m and takes 7 lines
            for (; have < 2 * m + 7; have++) {
                const unsigned char dx = d[(baseX + have) & 255]; float sx = 0, sy = 0, sz = 0; const int slot = (have & (RING1 - 1)) * 256 + t;
                for (int k = 0; k < planes; k++) { const int h = d[(unsigned char) (d[(unsigned char) (dx + (unsigned char) (y0 + k))] + t)] & 15; sx += GX[h] * wa[k]; sy += GY[h] * wb[k]; sz += GZ[h] * wa[k]; }
                ringX[slot] = sx; ringY[slot] = sy; ringZ[slot] = sz;
            }
            {
                float a = 0, c = 0;
                for (int r = 0; r < 7; r++) { const int slot = ((2 * m + r) & (RING1 - 1)) * 256 + t; a += kxb[r] * ringX[slot] + kxa[r] * ringY[slot]; c += kxa[r] * ringZ[slot]; }
                rowXY[t] = a; rowZ[t] = c;
            }
            __syncthreads();
            if (t < 128) {
                float sum = 0;
                for (int r = 0; r < 7; r++) sum += kza[r] * rowXY[zs[r]] + kzb[r] * rowZ[zs[r]];
                lowest = fminf(lowest, sum * norm);
            }
            __syncthreads();
        }
        if (t < 128) mins[t] = lowest;
        __syncthreads();
        if (t == 0) { float low = mins[0]; for (int i = 1; i < 128; i++) low = fminf(low, mins[i]); total += low; }
        __syncthreads();
    }
    if (t == 0) out[blockIdx.x] = total;
}

static TableSeed *deviceIn = nullptr; static float *deviceScratch = nullptr, *deviceOut = nullptr; static int capacity = 0; static float skipLevel = 9;
void gputables_skip(float level) { skipLevel = level; } // seeds whose 512-block number is above this get 1 as their 1280-block number (default: none)
static int inCapacity = 0;
int gputables_init(int maxSeeds, int maxPrelook) {
    capacity = maxSeeds; inCapacity = maxSeeds > maxPrelook ? maxSeeds : maxPrelook;
    if (cudaMalloc(&deviceIn, (size_t) inCapacity * sizeof(TableSeed)) != cudaSuccess || cudaMalloc(&deviceScratch, (size_t) maxSeeds * PER_BLOCK * sizeof(float)) != cudaSuccess || cudaMalloc(&deviceOut, (size_t) inCapacity * 2 * sizeof(float)) != cudaSuccess) {
        fprintf(stderr, "gputables: no GPU memory for %d seeds at once\n", maxSeeds); return 1;
    }
    return 0;
}
// out[i] = the cheap first number of seed i (octave -8 alone: the lowest 1280-block window of stack A plus that of stack B); 0 if all went well
int gpuprelook_batch(const TableSeed *in, int count, float *out) {
    if (count > inCapacity) return 1;
    if (cudaMemcpy(deviceIn, in, (size_t) count * sizeof(TableSeed), cudaMemcpyHostToDevice) != cudaSuccess) return 1;
    prelook<<<count, 256>>>(deviceIn, deviceOut);
    if (cudaMemcpy(out, deviceOut, (size_t) count * sizeof(float), cudaMemcpyDeviceToHost) != cudaSuccess) { fprintf(stderr, "gpuprelook: %s\n", cudaGetErrorString(cudaGetLastError())); return 1; }
    return 0;
}
// out[2 i] = the 512-block number of seed i, out[2 i + 1] = the 1280-block one; 0 if all went well
int gputables_batch(const TableSeed *in, int count, float *out) {
    if (count > capacity) return 1;
    if (cudaMemcpy(deviceIn, in, (size_t) count * sizeof(TableSeed), cudaMemcpyHostToDevice) != cudaSuccess) return 1;
    static const bool oldKernel = getenv("GPUTABLES_OLD") != nullptr; // version 3 of the kernel, for comparing
    if (oldKernel) tables<<<count, 256>>>(deviceIn, deviceScratch, deviceOut, skipLevel); else tables4<<<count, 256>>>(deviceIn, deviceScratch, deviceOut, skipLevel);
    if (cudaMemcpy(out, deviceOut, (size_t) count * 2 * sizeof(float), cudaMemcpyDeviceToHost) != cudaSuccess) { fprintf(stderr, "gputables: %s\n", cudaGetErrorString(cudaGetLastError())); return 1; }
    return 0;
}
