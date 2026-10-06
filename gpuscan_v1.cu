// The phase gate of the cave search on a GPU: which seeds have the y phases of the four lowest cave_cheese layers
// (octaves -8 and -7, stacks A and B) inside their windows. Pure 64-bit arithmetic (the steps of seedgate5's phases():
// xSetSeed, the world pair, the noise's source, a fork per stack, the second double of each layer's source), so a small
// GPU does what a dozen CPU threads did. The windows are integers (the phase is the low 45 bits of the double's
// mantissa plus an offset; a little wider than the exact windows): the host redoes the survivors exactly.
// build: nvcc -O3 -c gpuscan.cu -o gpuscan.o        (linked into seedgate6)
#include <cstdint>
#include <cstdio>
#include <cuda_runtime.h>
#include "gpuscan.h"

__device__ __forceinline__ uint64_t rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }
__device__ __forceinline__ uint64_t nextLong(uint64_t &lo, uint64_t &hi) {
    uint64_t l = lo, h = hi, n = rotl(l + h, 17) + l;
    h ^= l; lo = rotl(l, 49) ^ h ^ (h << 21); hi = rotl(h, 28);
    return n;
}
// the second output of a source that starts from (lo, hi), reduced to the 45 bits that are the fraction of double x 256
__device__ __forceinline__ int64_t secondFraction(uint64_t lo, uint64_t hi) {
    uint64_t h = hi ^ lo, l = rotl(lo, 49) ^ h ^ (h << 21); h = rotl(h, 28);
    return (int64_t) (((rotl(l + h, 17) + l) >> 11) & ((1ULL << 45) - 1));
}
__device__ __forceinline__ uint64_t stafford(uint64_t x) {
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL; x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}
__global__ void scan(uint64_t base, int perThread, GpuScanWindows w, uint64_t *out, uint32_t *count, uint32_t capacity) {
    uint64_t first = base + ((uint64_t) blockIdx.x * blockDim.x + threadIdx.x) * (uint64_t) perThread;
    for (int k = 0; k < perThread; k++) {
        uint64_t seed = first + k, lo = seed ^ 0x6a09e667f3bcc909ULL, hi = lo + 0x9e3779b97f4a7c15ULL;
        lo = stafford(lo); hi = stafford(hi);
        uint64_t worldLo = nextLong(lo, hi), worldHi = nextLong(lo, hi);
        uint64_t rl = worldLo ^ w.cheeseLo, rh = worldHi ^ w.cheeseHi;
        uint64_t forkLo = nextLong(rl, rh), forkHi = nextLong(rl, rh);                       // stack A
        int64_t v = secondFraction(forkLo ^ w.o8Lo, forkHi ^ w.o8Hi);
        if (v <= w.low[0] || v >= w.high[0]) continue;
        v = secondFraction(forkLo ^ w.o7Lo, forkHi ^ w.o7Hi);
        if (v <= w.low[1] || v >= w.high[1]) continue;
        forkLo = nextLong(rl, rh); forkHi = nextLong(rl, rh);                                // stack B
        v = secondFraction(forkLo ^ w.o8Lo, forkHi ^ w.o8Hi);
        if (v <= w.low[2] || v >= w.high[2]) continue;
        v = secondFraction(forkLo ^ w.o7Lo, forkHi ^ w.o7Hi);
        if (v <= w.low[3] || v >= w.high[3]) continue;
        uint32_t at = atomicAdd(count, 1u);
        if (at < capacity) out[at] = seed;
    }
}

static uint64_t *deviceOut = nullptr; static uint32_t *deviceCount = nullptr; static uint32_t capacity = 0; static GpuScanWindows windows;
int gpuscan_init(const GpuScanWindows *w, uint32_t cap) {
    windows = *w; capacity = cap;
    if (cudaMalloc(&deviceOut, (size_t) cap * sizeof(uint64_t)) != cudaSuccess || cudaMalloc(&deviceCount, sizeof(uint32_t)) != cudaSuccess) { fprintf(stderr, "gpuscan: no GPU memory\n"); return 1; }
    return 0;
}
// the seeds of base .. base + threads x perThread - 1 that pass, into out (at most the capacity given to gpuscan_init);
// returns how many passed, or -1 on a CUDA error
long gpuscan_batch(uint64_t base, uint32_t threads, int perThread, uint64_t *out) {
    if (cudaMemset(deviceCount, 0, sizeof(uint32_t)) != cudaSuccess) return -1;
    const int block = 256;
    scan<<<threads / block, block>>>(base, perThread, windows, deviceOut, deviceCount, capacity);
    uint32_t n = 0;
    if (cudaMemcpy(&n, deviceCount, sizeof n, cudaMemcpyDeviceToHost) != cudaSuccess) { fprintf(stderr, "gpuscan: %s\n", cudaGetErrorString(cudaGetLastError())); return -1; }
    uint32_t take = n < capacity ? n : capacity;
    if (take && cudaMemcpy(out, deviceOut, (size_t) take * sizeof(uint64_t), cudaMemcpyDeviceToHost) != cudaSuccess) return -1;
    return (long) n;
}
