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
// gputables_v1.cu). Then the 4 x 4 window means and the two pair counts by histogram, as the CPU code does them.
// build: nvcc -O3 -c gputables.cu -o gputables.o
#include <cstdint>
#include <cstdio>
#include <cuda_runtime.h>
#include "gpuscan.h"

static const int PLANE = 256 * 256, BINS = 4096, PER_BLOCK = 5 * PLANE + 2 * BINS, RING = 8;

// the smallest window-mean sum a + b that at least `pairs` pairs stay under (bins of 1/4096 above each field's minimum)
__device__ float pairQuantile(const float *a, const float *b, uint64_t pairs, float *minA, float *minB, unsigned int *histA, unsigned int *histB, unsigned long long *part, int *range) {
    const int t = threadIdx.x; float la = a[t], lb = b[t];
    for (int j = 1; j < 256; j++) { la = fminf(la, a[j * 256 + t]); lb = fminf(lb, b[j * 256 + t]); }
    minA[t] = la; minB[t] = lb;
    for (int i = t; i < BINS; i += 256) histA[i] = histB[i] = 0;
    __syncthreads();
    if (t == 0) { for (int i = 1; i < 256; i++) { la = fminf(la, minA[i]); lb = fminf(lb, minB[i]); } minA[0] = la; minB[0] = lb; }
    __syncthreads();
    la = minA[0]; lb = minB[0];
    for (int j = 0; j < 256; j++) {
        int bin = (int) ((a[j * 256 + t] - la) * 4096.0f); if (bin < BINS) atomicAdd(&histA[bin], 1u);
        bin = (int) ((b[j * 256 + t] - lb) * 4096.0f); if (bin < BINS) atomicAdd(&histB[bin], 1u);
    }
    __syncthreads();
    if (t == 0) { for (int i = 1; i < BINS; i++) histB[i] += histB[i - 1]; range[0] = 0; range[1] = BINS - 1; }
    __syncthreads();
    for (int step = 0; step < 12; step++) { // 4096 bins: 12 halvings
        const int mid = (range[0] + range[1]) / 2; unsigned long long sum = 0;
        for (int i = t; i <= mid; i += 256) sum += (unsigned long long) histA[i] * histB[mid - i];
        part[t] = sum;
        __syncthreads();
        if (t == 0) { unsigned long long total = 0; for (int i = 0; i < 256; i++) total += part[i]; if (total >= pairs) range[1] = mid; else range[0] = mid + 1; }
        __syncthreads();
    }
    return la + lb + (range[0] + 1) / 4096.0f;
}

__global__ void tables(const TableSeed *in, float *scratch, float *out) {
    static const float GX[16] = {1, -1, 1, -1, 1, -1, 1, -1, 0, 0, 0, 0, 1, 0, -1, 0}, GY[16] = {1, 1, -1, -1, 0, 0, 0, 0, 1, -1, 1, -1, 1, -1, 1, -1}, GZ[16] = {0, 0, 0, 0, 1, 1, -1, -1, 1, 1, -1, -1, 0, 1, 0, -1};
    __shared__ unsigned char d[256]; __shared__ float ringX[RING][256], ringY[RING][256], ringZ[RING][256], rowXY[256], rowZ[256], minA[256], minB[256]; __shared__ unsigned long long part[256]; __shared__ int range[2];
    const int t = threadIdx.x; const TableSeed &seed = in[blockIdx.x]; float *base = scratch + (size_t) blockIdx.x * PER_BLOCK;
    float *field[2] = {base, base + PLANE}, *wide[2] = {base + 2 * PLANE, base + 3 * PLANE}, *temp = base + 4 * PLANE;
    unsigned int *histA = (unsigned int *) (base + 5 * PLANE), *histB = histA + BINS;
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
                const unsigned char dx = d[(baseX + have) & 255]; float sx = 0, sy = 0, sz = 0;
                for (int k = 0; k < planes; k++) { const int h = d[(unsigned char) (d[(unsigned char) (dx + (unsigned char) (y0 + k))] + t)] & 15; sx += GX[h] * wa[k]; sy += GY[h] * wb[k]; sz += GZ[h] * wa[k]; }
                ringX[have & (RING - 1)][t] = sx; ringY[have & (RING - 1)][t] = sy; ringZ[have & (RING - 1)][t] = sz;
            }
            { // along x: the window that starts at line baseX + m x cells, for the corner line z = t
                float a = 0, c = 0;
                for (int r = 0; r < taps; r++) { const int slot = (m * cells + r) & (RING - 1); a += kxb[r] * ringX[slot][t] + kxa[r] * ringY[slot][t]; c += kxa[r] * ringZ[slot][t]; }
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
    for (int half = 0; half < 2; half++) { // the 1280-block windows: 4 x 4 of the 512-block ones, along the rows and then along the columns
        const float *f = field[half]; float *w = wide[half];
        for (int i = 0; i < 256; i++) temp[i * 256 + t] = f[i * 256 + t] + f[i * 256 + ((t + 1) & 255)] + f[i * 256 + ((t + 2) & 255)] + f[i * 256 + ((t + 3) & 255)];
        __syncthreads();
        for (int i = 0; i < 256; i++) w[i * 256 + t] = (temp[i * 256 + t] + temp[((i + 1) & 255) * 256 + t] + temp[((i + 2) & 255) * 256 + t] + temp[((i + 3) & 255) * 256 + t]) * 0.0625f;
        __syncthreads();
    }
    float narrow = pairQuantile(field[0], field[1], 11544, minA, minB, histA, histB, part, range);
    __syncthreads();
    float broad = pairQuantile(wide[0], wide[1], 62, minA, minB, histA, histB, part, range);
    if (t == 0) { out[2 * blockIdx.x] = narrow; out[2 * blockIdx.x + 1] = broad; }
}

static TableSeed *deviceIn = nullptr; static float *deviceScratch = nullptr, *deviceOut = nullptr; static int capacity = 0;
int gputables_init(int maxSeeds) {
    capacity = maxSeeds;
    if (cudaMalloc(&deviceIn, (size_t) maxSeeds * sizeof(TableSeed)) != cudaSuccess || cudaMalloc(&deviceScratch, (size_t) maxSeeds * PER_BLOCK * sizeof(float)) != cudaSuccess || cudaMalloc(&deviceOut, (size_t) maxSeeds * 2 * sizeof(float)) != cudaSuccess) {
        fprintf(stderr, "gputables: no GPU memory for %d seeds at once\n", maxSeeds); return 1;
    }
    return 0;
}
// out[2 i] = the 512-block number of seed i, out[2 i + 1] = the 1280-block one; 0 if all went well
int gputables_batch(const TableSeed *in, int count, float *out) {
    if (count > capacity) return 1;
    if (cudaMemcpy(deviceIn, in, (size_t) count * sizeof(TableSeed), cudaMemcpyHostToDevice) != cudaSuccess) return 1;
    tables<<<count, 256>>>(deviceIn, deviceScratch, deviceOut);
    if (cudaMemcpy(out, deviceOut, (size_t) count * 2 * sizeof(float), cudaMemcpyDeviceToHost) != cudaSuccess) { fprintf(stderr, "gputables: %s\n", cudaGetErrorString(cudaGetLastError())); return 1; }
    return 0;
}
