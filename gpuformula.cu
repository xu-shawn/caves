// The formula stage of the cave search on a GPU: see gputerrain.h. One thread per column of a site's grid; the noise is
// cavenoise.hpp's fastPerlin with the lattice split in double precision and the rest in single (a sample differs from
// the CPU's by about 1e-7: one open sample in a million may flip).
// build: nvcc -O3 -arch=sm_89 -c gpuformula.cu -o gpuformula.o
#include <cstdio>
#include <cuda_runtime.h>
#include "gputerrain.h"

__device__ __forceinline__ float perlin3(const FormulaLayer &o, double x, double y, double z) {
    static const float GX[16] = {1, -1, 1, -1, 1, -1, 1, -1, 0, 0, 0, 0, 1, 0, -1, 0}, GY[16] = {1, 1, -1, -1, 0, 0, 0, 0, 1, -1, 1, -1, 1, -1, 1, -1}, GZ[16] = {0, 0, 0, 0, 1, 1, -1, -1, 1, 1, -1, -1, 0, 1, 0, -1};
    x = x * o.frequency + o.a; y = y * o.frequency + o.b; z = z * o.frequency + o.c;
    const double i1 = floor(x), i2 = floor(y), i3 = floor(z);
    const float d1 = (float) (x - i1), d2 = (float) (y - i2), d3 = (float) (z - i3);
    const int h1 = (int) (long long) i1 & 255, h2 = (int) (long long) i2 & 255, h3 = (int) (long long) i3 & 255;
    const float t1 = d1 * d1 * d1 * (d1 * (d1 * 6.0f - 15.0f) + 10.0f), t2 = d2 * d2 * d2 * (d2 * (d2 * 6.0f - 15.0f) + 10.0f), t3 = d3 * d3 * d3 * (d3 * (d3 * 6.0f - 15.0f) + 10.0f);
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
// the open samples of a column: bit l for y = -56 + 8 l, l = 1 .. 32 (formulacave's test without its surface limit)
__global__ void formulaColumns(const FormulaSeed *seed, const int32_t *sites, int count, uint64_t *out) {
    const int t = blockIdx.x * blockDim.x + threadIdx.x, site = t / FORMULA_COLUMNS, column = t % FORMULA_COLUMNS; if (site >= count) return;
    const FormulaSeed &s = *seed; const double x = sites[2 * site] + 16.0 * (column % 96), z = sites[2 * site + 1] + 16.0 * (column / 96); uint64_t bits = 0;
    for (int l = 1; l <= 32; l++) {
        const double y = -56 + 8 * l; float v = 0.27f;
        for (int i = 0; i < s.cheeseCount; i++) v += perlin3(s.cheese[i], x, y * 0.6666666666666666, z);
        if (v >= 0) continue;
        if (v < -1) v = -1;
        const float ly = perlin3(s.layer[0], x, y * 8.0, z) + perlin3(s.layer[1], x, y * 8.0, z);
        if (v + 4 * ly * ly >= 0) continue;
        if (perlin3(s.flood[0], x, y * 0.67, z) + perlin3(s.flood[1], x, y * 0.67, z) > 0.4f) continue;
        bits |= 1ULL << l;
    }
    out[t] = bits;
}

static FormulaSeed *dSeed; static int32_t *dSites; static uint64_t *dOut, *hostOut; static int capacity = 0;
int gpuformula_init(int maxSites) {
    capacity = maxSites;
    if (cudaMalloc(&dSeed, sizeof(FormulaSeed)) != cudaSuccess || cudaMalloc(&dSites, (size_t) maxSites * 2 * sizeof(int32_t)) != cudaSuccess || cudaMalloc(&dOut, (size_t) maxSites * FORMULA_COLUMNS * sizeof(uint64_t)) != cudaSuccess ||
        cudaHostAlloc(&hostOut, (size_t) maxSites * FORMULA_COLUMNS * sizeof(uint64_t), cudaHostAllocDefault) != cudaSuccess) { fprintf(stderr, "gpuformula: no memory\n"); return 1; }
    return 0;
}
const uint64_t *gpuformula_run(const FormulaSeed *seed, const int32_t *sites, int count) {
    if (count <= 0 || count > capacity) return NULL;
    if (cudaMemcpy(dSeed, seed, sizeof(FormulaSeed), cudaMemcpyHostToDevice) != cudaSuccess || cudaMemcpy(dSites, sites, (size_t) count * 2 * sizeof(int32_t), cudaMemcpyHostToDevice) != cudaSuccess) return NULL;
    formulaColumns<<<(count * FORMULA_COLUMNS + 127) / 128, 128>>>(dSeed, dSites, count, dOut);
    if (cudaMemcpy(hostOut, dOut, (size_t) count * FORMULA_COLUMNS * sizeof(uint64_t), cudaMemcpyDeviceToHost) != cudaSuccess) { fprintf(stderr, "gpuformula: %s\n", cudaGetErrorString(cudaGetLastError())); return NULL; }
    return hostOut;
}
