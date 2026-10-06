// The pair scanner's terrain step on a GPU: see gputerrain.h. One thread per place. Positions are integers (a point of
// the window is at quart floor(16384 (g + 4 u + 2) / 1011)), and the lattice coordinate of a Perlin octave is made in
// 64-bit fixed point, so nothing needs double precision (which a consumer card does at a sixty-fourth of the speed);
// the noise itself and the spline are single precision: the estimates differ from the CPU's by about 1e-4 blocks.
// build: nvcc -O3 -c gputerrain.cu -o gputerrain.o
#include <cstdio>
#include <cuda_runtime.h>
#include "gputerrain.h"

__device__ __forceinline__ float perlin(const TerrainOctave &o, int64_t qx, int64_t qz) {
    static const float GX[16] = {1, -1, 1, -1, 1, -1, 1, -1, 0, 0, 0, 0, 1, 0, -1, 0}, GY[16] = {1, 1, -1, -1, 0, 0, 0, 0, 1, -1, 1, -1, 1, -1, 1, -1}, GZ[16] = {0, 0, 0, 0, 1, 1, -1, -1, 1, 1, -1, -1, 0, 1, 0, -1};
    const int64_t x = qx * o.lac + o.a, z = qz * o.lac + o.c;
    const int h1 = (int) (x >> 40) & 255, h3 = (int) (z >> 40) & 255, h2 = o.h2;
    const float d1 = (float) ((x & 0xFFFFFFFFFFLL) >> 16) * (1.0f / 16777216.0f), d3 = (float) ((z & 0xFFFFFFFFFFLL) >> 16) * (1.0f / 16777216.0f), d2 = o.d2;
    const float t1 = d1 * d1 * d1 * (d1 * (d1 * 6.0f - 15.0f) + 10.0f), t3 = d3 * d3 * d3 * (d3 * (d3 * 6.0f - 15.0f) + 10.0f), t2 = o.t2;
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
// cubiomes' getSpline on the flattened tree (D: the levels left; the tree is 5 deep at most, checked by the host)
template <int D> struct SplineAt {
    __device__ static float get(const FlatSpline *nodes, int i, const float *vals) {
        const FlatSpline &sp = nodes[i];
        if (sp.len == 1) return sp.val;
        const float f = vals[sp.typ]; int k = 0;
        for (; k < sp.len; k++) if (sp.loc[k] >= f) break;
        if (k == 0 || k == sp.len) { if (k) k--; return SplineAt<D - 1>::get(nodes, sp.child[k], vals) + sp.der[k] * (f - sp.loc[k]); }
        const float g = sp.loc[k - 1], h = sp.loc[k], t = (f - g) / (h - g), l = sp.der[k - 1], m = sp.der[k];
        const float n = SplineAt<D - 1>::get(nodes, sp.child[k - 1], vals), o = SplineAt<D - 1>::get(nodes, sp.child[k], vals);
        const float p = l * (h - g) - (o - n), q = -m * (h - g) + (o - n);
        return (n + t * (o - n)) + t * (1.0f - t) * (p + t * (q - p));
    }
};
template <> struct SplineAt<0> { __device__ static float get(const FlatSpline *nodes, int i, const float *) { return nodes[i].val; } };

__device__ __forceinline__ int64_t quart(int g, int u) { const int64_t n = 16384LL * (g + 4 * u + 2); return n >= 0 ? n / 1011 : -((-n + 1010) / 1011); }
__device__ float surfaceAt(const TerrainSeed &s, const FlatSpline *nodes, int gi, int gj, int u, int v) {
    const int64_t qx = quart(gi, u), qz = quart(gj, v); float c = 0, e = 0, w = 0;
    for (int i = 0; i < 6; i++) c += perlin(s.octave[i], qx, qz);
    for (int i = 6; i < 10; i++) e += perlin(s.octave[i], qx, qz);
    for (int i = 10; i < 14; i++) w += perlin(s.octave[i], qx, qz);
    c *= s.scale[0]; e *= s.scale[1]; w *= s.scale[2];
    const float vals[4] = {c, e, -3.0f * (fabsf(fabsf(w) - 0.6666667f) - 0.33333334f), w};
    return 128.0f * (1.0f - 83.0f / 160.0f + SplineAt<6>::get(nodes, 0, vals) + 0.015f);
}
// the first four points of each place and the scanner's three tests on them: out = their sum, or negative (out)
__global__ void firstFour(const TerrainSeed *seed, const FlatSpline *nodes, const TerrainPlace *places, int count, float *out) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x; if (i >= count) return;
    const TerrainPlace p = places[i]; const TerrainSeed &s = *seed;
    float sum = surfaceAt(s, nodes, p.gi, p.gj, 0, 1), result = -1.0f;
    if (sum >= p.need - 40.0f) {
        sum += surfaceAt(s, nodes, p.gi, p.gj, 3, 2);
        if (sum >= 2.0f * (p.need - 25.0f)) {
            sum += surfaceAt(s, nodes, p.gi, p.gj, 1, 3); sum += surfaceAt(s, nodes, p.gi, p.gj, 2, 0);
            if (sum >= 4.0f * fmaxf(64.0f, p.need - 12.0f)) result = sum;
        }
    }
    out[i] = result;
}
// the other twelve for the places still in (list[k]: the place's index): out[list[k]] = the mean of the 16, or negative
__global__ void otherTwelve(const TerrainSeed *seed, const FlatSpline *nodes, const TerrainPlace *places, const int *list, int count, float *out) {
    const int k = blockIdx.x * blockDim.x + threadIdx.x; if (k >= count) return;
    const int i = list[k]; const TerrainPlace p = places[i]; const TerrainSeed &s = *seed; float sum = out[i];
    for (int u = 0; u < 4; u++) for (int v = 0; v < 4; v++) if (!((u == 0 && v == 1) || (u == 3 && v == 2) || (u == 1 && v == 3) || (u == 2 && v == 0))) sum += surfaceAt(s, nodes, p.gi, p.gj, u, v);
    out[i] = sum >= 16.0f * p.need ? sum * 0.0625f : -1.0f;
}

static TerrainSeed *dSeed; static FlatSpline *dNodes; static TerrainPlace *dPlaces; static int *dList; static float *dOut; static int capacity = 0; static int *hostList;
int gputerrain_init(const FlatSpline *nodes, int count, int maxPlaces) {
    capacity = maxPlaces;
    if (cudaMalloc(&dSeed, sizeof(TerrainSeed)) != cudaSuccess || cudaMalloc(&dNodes, (size_t) count * sizeof(FlatSpline)) != cudaSuccess || cudaMalloc(&dPlaces, (size_t) maxPlaces * sizeof(TerrainPlace)) != cudaSuccess ||
        cudaMalloc(&dList, (size_t) maxPlaces * sizeof(int)) != cudaSuccess || cudaMalloc(&dOut, (size_t) maxPlaces * sizeof(float)) != cudaSuccess) { fprintf(stderr, "gputerrain: no GPU memory\n"); return 1; }
    hostList = new int[maxPlaces];
    return cudaMemcpy(dNodes, nodes, (size_t) count * sizeof(FlatSpline), cudaMemcpyHostToDevice) != cudaSuccess;
}
int gputerrain_run(const TerrainSeed *seed, const TerrainPlace *places, int count, float *surface) {
    if (count > capacity) return 1;
    if (count == 0) return 0;
    if (cudaMemcpy(dSeed, seed, sizeof(TerrainSeed), cudaMemcpyHostToDevice) != cudaSuccess || cudaMemcpy(dPlaces, places, (size_t) count * sizeof(TerrainPlace), cudaMemcpyHostToDevice) != cudaSuccess) return 1;
    firstFour<<<(count + 255) / 256, 256>>>(dSeed, dNodes, dPlaces, count, dOut);
    if (cudaMemcpy(surface, dOut, (size_t) count * sizeof(float), cudaMemcpyDeviceToHost) != cudaSuccess) { fprintf(stderr, "gputerrain: %s\n", cudaGetErrorString(cudaGetLastError())); return 1; }
    int kept = 0; for (int i = 0; i < count; i++) if (surface[i] >= 0) hostList[kept++] = i;
    if (kept == 0) return 0;
    if (cudaMemcpy(dList, hostList, (size_t) kept * sizeof(int), cudaMemcpyHostToDevice) != cudaSuccess) return 1;
    otherTwelve<<<(kept + 255) / 256, 256>>>(dSeed, dNodes, dPlaces, dList, kept, dOut);
    if (cudaMemcpy(surface, dOut, (size_t) count * sizeof(float), cudaMemcpyDeviceToHost) != cudaSuccess) { fprintf(stderr, "gputerrain: %s\n", cudaGetErrorString(cudaGetLastError())); return 1; }
    return 0;
}
