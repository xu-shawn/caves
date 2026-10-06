// The pair scanner's coarse tables and its formula step on a GPU: see gputerrain.h. Steps 1 and 3 of cavepairs: a column
// of the 64.8-block grid gets the cheese noise of its stack at 12 heights (y -40 + 16 k; two base octaves, whose sum
// over the 10 lowest heights goes back for the window means, and two more), cave_layer and the floodedness, packed as
// the CPU packs them (cheese in 1/4096, the others in 1/64). Every column is made complete: on the CPU that was only
// worth it for the columns under chosen windows. Then, for each pair of windows the host picked, the open points per
// height over its 16 x 16 columns: the formula in 16-bit arithmetic with the saturating adds of the CPU's AVX2 code.
// build: nvcc -O3 -arch=sm_89 -c gpupairs.cu -o gpupairs.o
#include <cstdio>
#include <cuda_runtime.h>
#include "gputerrain.h"

struct Packed { int16_t cheese[16]; int8_t layer[16], flood[16]; };
static const double STEP = 65536.0 / 1011.0;
// A column's lattice position along an axis for a layer: the cell (mod 256), the place inside it and its fade. The grid
// is the same for every seed up to the layers' offsets, so the host makes these per layer and axis index (12 layers x
// 1,011 x two axes, and 12 heights) and the kernel needs no double precision (which this card does at a sixty-fourth
// of its single-precision speed: with the lattice split in the kernel the tables took 35 ms a seed).
struct Axis { float d, t; int h; };
static const int LAYERS = 12; // per stack: the two base cheese layers, the two more, cave_layer, floodedness
struct Lattice { Axis x[LAYERS][PAIR_NA], z[LAYERS][PAIR_NA], y[LAYERS][12]; };
__device__ __forceinline__ float perlinAt(const FormulaLayer &o, const Axis &X, const Axis &Y, const Axis &Z) {
    static const float GX[16] = {1, -1, 1, -1, 1, -1, 1, -1, 0, 0, 0, 0, 1, 0, -1, 0}, GY[16] = {1, 1, -1, -1, 0, 0, 0, 0, 1, -1, 1, -1, 1, -1, 1, -1}, GZ[16] = {0, 0, 0, 0, 1, 1, -1, -1, 1, 1, -1, -1, 0, 1, 0, -1};
    const float d1 = X.d, d2 = Y.d, d3 = Z.d, t1 = X.t, t2 = Y.t, t3 = Z.t; const int h1 = X.h, h2 = Y.h, h3 = Z.h;
    const uint8_t *idx = o.d;
    const int a1 = (idx[h1] + h2) & 255, b1 = (idx[h1 + 1] + h2) & 255;
    const int a2 = (idx[a1] + h3) & 255, a3 = (idx[a1 + 1] + h3) & 255, b2 = (idx[b1] + h3) & 255, b3 = (idx[b1 + 1] + h3) & 255;
#define G(i, x, y, z) (GX[(i) & 15] * (x) + GY[(i) & 15] * (y) + GZ[(i) & 15] * (z))
    float l1 = G(idx[a2], d1, d2, d3), l2 = G(idx[b2], d1 - 1, d2, d3), l3 = G(idx[a3], d1, d2 - 1, d3), l4 = G(idx[b3], d1 - 1, d2 - 1, d3);
    float l5 = G(idx[a2 + 1], d1, d2, d3 - 1), l6 = G(idx[b2 + 1], d1 - 1, d2, d3 - 1), l7 = G(idx[a3 + 1], d1, d2 - 1, d3 - 1), l8 = G(idx[b3 + 1], d1 - 1, d2 - 1, d3 - 1);
#undef G
    l1 += t1 * (l2 - l1); l3 += t1 * (l4 - l3); l5 += t1 * (l6 - l5); l7 += t1 * (l8 - l7);
    l1 += t2 * (l3 - l1); l5 += t2 * (l7 - l5);
    return o.amplitude * (l1 + t3 * (l5 - l1));
}
__device__ __forceinline__ float clampTo(float v, float limit) { return fmaxf(-limit, fminf(limit, v)); }
// thread t: a column of stack A (t < NA x NA) or B
__global__ void makeTables(const FormulaSeed *seed, const Lattice *lattice, Packed *columns, float *sums) {
    const int t = blockIdx.x * blockDim.x + threadIdx.x; if (t >= PAIR_COLUMNS) return;
    const int half = t >= PAIR_NA * PAIR_NA, c = half ? t - PAIR_NA * PAIR_NA : t, n = half ? PAIR_NB : PAIR_NA, i = c / n, j = c % n, first = 6 * half; const FormulaSeed &s = *seed; const Lattice &L = *lattice;
    Packed p; float sum = 0;
    for (int k = 0; k < 16; k++) {
        if (k >= 12) { p.cheese[k] = 4096; p.layer[k] = p.flood[k] = 0; continue; } // not made: closed
        float base = 0, more = 0;
        for (int o = 0; o < 2; o++) if (half + 2 * o < s.cheeseCount) base += perlinAt(s.cheese[half + 2 * o], L.x[first + o][i], L.y[first + o][k], L.z[first + o][j]);
        for (int o = 2; o < 4; o++) if (half + 2 * o < s.cheeseCount) more += perlinAt(s.cheese[half + 2 * o], L.x[first + o][i], L.y[first + o][k], L.z[first + o][j]);
        if (k < 10) sum += base;
        long cheese = lrintf(clampTo(base, 3.9f) * 4096) + lrintf(clampTo(more, 3.9f) * 4096);
        p.cheese[k] = (int16_t) (cheese < -32000 ? -32000 : (cheese > 32000 ? 32000 : cheese));
        p.layer[k] = (int8_t) lrintf(clampTo(perlinAt(s.layer[half], L.x[first + 4][i], L.y[first + 4][k], L.z[first + 4][j]), 1.98f) * 64);
        p.flood[k] = (int8_t) lrintf(clampTo(perlinAt(s.flood[half], L.x[first + 5][i], L.y[first + 5][k], L.z[first + 5][j]), 1.98f) * 64);
    }
    columns[t] = p; sums[t] = sum;
}
__device__ __forceinline__ int saturate(int v) { return v < -32768 ? -32768 : (v > 32767 ? 32767 : v); }
// thread i: a pair of windows (origins a, b): open if clamp(cheese + 0.27, -1, ..) + 4 layer^2 < 0 and floodedness <= 0.4
__global__ void pairFormula(const Packed *columns, const int32_t *pairs, int count, int16_t *out) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x; if (i >= count) return;
    const int a = pairs[2 * i], b = pairs[2 * i + 1], ia = a / PAIR_NA, ja = a % PAIR_NA, ib = b / PAIR_NB, jb = b % PAIR_NB; const Packed *tableA = columns, *tableB = columns + PAIR_NA * PAIR_NA;
    int counts[16]; for (int k = 0; k < 16; k++) counts[k] = 0;
    for (int di = 0; di < 16; di++) {
        const Packed *rowA = tableA + (size_t) ((ia + di) % PAIR_NA) * PAIR_NA, *rowB = tableB + (size_t) ((ib + di) % PAIR_NB) * PAIR_NB;
        for (int dj = 0; dj < 16; dj++) {
            const Packed &pa = rowA[(ja + dj) % PAIR_NA], &pb = rowB[(jb + dj) % PAIR_NB];
            for (int k = 0; k < 12; k++) { // (the heights above the 12 that are made are closed in every column)
                int c = saturate(saturate(pa.cheese[k] + pb.cheese[k]) + 1106); if (c < -4096) c = -4096;
                int l = pa.layer[k] + pb.layer[k]; if (l < 0) l = -l; if (l > 90) l = 90;
                const int value = saturate(c + 4 * l * l), wet = pa.flood[k] + pb.flood[k];
                counts[k] += value < 0 && wet < 26;
            }
        }
    }
    for (int k = 0; k < 16; k++) out[16 * i + k] = (int16_t) counts[k];
}

static FormulaSeed *dSeed; static Lattice *dLattice, *hostLattice; static Packed **dTables; static float *dSums, *hostSums; static int32_t *dPairs; static int16_t *dCounts, *hostCounts; static int slotCount = 0, pairCapacity = 0;
int gpupairs_init(int slots, int maxPairs) {
    slotCount = slots; pairCapacity = maxPairs; dTables = new Packed *[slots];
    for (int i = 0; i < slots; i++) if (cudaMalloc(&dTables[i], (size_t) PAIR_COLUMNS * sizeof(Packed)) != cudaSuccess) { fprintf(stderr, "gpupairs: no GPU memory for %d table slots\n", slots); return 1; }
    hostLattice = new Lattice;
    if (cudaMalloc(&dSeed, sizeof(FormulaSeed)) != cudaSuccess || cudaMalloc(&dLattice, sizeof(Lattice)) != cudaSuccess || cudaMalloc(&dSums, (size_t) PAIR_COLUMNS * sizeof(float)) != cudaSuccess || cudaHostAlloc(&hostSums, (size_t) PAIR_COLUMNS * sizeof(float), cudaHostAllocDefault) != cudaSuccess ||
        cudaMalloc(&dPairs, (size_t) maxPairs * 2 * sizeof(int32_t)) != cudaSuccess || cudaMalloc(&dCounts, (size_t) maxPairs * 16 * sizeof(int16_t)) != cudaSuccess || cudaHostAlloc(&hostCounts, (size_t) maxPairs * 16 * sizeof(int16_t), cudaHostAllocDefault) != cudaSuccess) { fprintf(stderr, "gpupairs: no memory\n"); return 1; }
    return 0;
}
const float *gpupairs_tables(int slot, const FormulaSeed *seed) {
    if (slot < 0 || slot >= slotCount) return NULL;
    for (int half = 0; half < 2; half++) for (int o = 0; o < 6; o++) { // the lattice positions, in double precision here
        const FormulaLayer &layer = o < 4 ? seed->cheese[half + 2 * o] : (o == 4 ? seed->layer[half] : seed->flood[half]); const double yScale = o < 4 ? 0.6666666666666666 : (o == 4 ? 8.0 : 0.67);
        auto at = [](double v, Axis &out) { const double cell = floor(v), d = v - cell; out.h = (int) (long long) cell & 255; out.d = (float) d; out.t = (float) d; out.t = out.d * out.d * out.d * (out.d * (out.d * 6.0f - 15.0f) + 10.0f); };
        for (int i = 0; i < PAIR_NA; i++) { at(i * STEP * layer.frequency + layer.a, hostLattice->x[6 * half + o][i]); at(i * STEP * layer.frequency + layer.c, hostLattice->z[6 * half + o][i]); }
        for (int k = 0; k < 12; k++) at((-40 + 16 * k) * yScale * layer.frequency + layer.b, hostLattice->y[6 * half + o][k]);
    }
    if (cudaMemcpy(dSeed, seed, sizeof(FormulaSeed), cudaMemcpyHostToDevice) != cudaSuccess || cudaMemcpy(dLattice, hostLattice, sizeof(Lattice), cudaMemcpyHostToDevice) != cudaSuccess) return NULL;
    makeTables<<<(PAIR_COLUMNS + 127) / 128, 128>>>(dSeed, dLattice, dTables[slot], dSums);
    if (cudaMemcpy(hostSums, dSums, (size_t) PAIR_COLUMNS * sizeof(float), cudaMemcpyDeviceToHost) != cudaSuccess) { fprintf(stderr, "gpupairs: %s\n", cudaGetErrorString(cudaGetLastError())); return NULL; }
    return hostSums;
}
const int16_t *gpupairs_formula(int slot, const int32_t *pairs, int count) {
    if (slot < 0 || slot >= slotCount || count <= 0 || count > pairCapacity || cudaMemcpy(dPairs, pairs, (size_t) count * 2 * sizeof(int32_t), cudaMemcpyHostToDevice) != cudaSuccess) return NULL;
    pairFormula<<<(count + 127) / 128, 128>>>(dTables[slot], dPairs, count, dCounts);
    if (cudaMemcpy(hostCounts, dCounts, (size_t) count * 16 * sizeof(int16_t), cudaMemcpyDeviceToHost) != cudaSuccess) { fprintf(stderr, "gpupairs: %s\n", cudaGetErrorString(cudaGetLastError())); return NULL; }
    return hostCounts;
}
