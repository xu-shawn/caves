// The first coarse look of the seed gate on a GPU, second version: the number of gpulook.cu (for each seed the lowest
// tapered 1280-block window mean of octave -8 of stack A plus that of stack B, windows every 512 blocks, from the y
// components of the lattice gradients) with the z filter made once per stack (the idea of cpuprelook2.hpp).
// A lattice line at x index i and y plane k reads the table's gradients d[(e + z) & 255] along z, e = d[(d[x] + y) & 255]
// being one byte per line and plane: every line is the same ring of 256 gradients, turned by e. So
//     table[k][j] = wyB[k] x sum_r kz[r] GY[d[(j + r) & 255]]      (256 values per plane, made once)
// already holds the 7-tap window sum along z, and for window column c a line is  sum_k table[k][(e_k + 2 c) & 255].
// One block of 256 threads per seed as before, but now a thread is a window column of one stack (128 columns x 2
// stacks): it walks the 261 lines along x with its own ring of 8 lines in registers and takes the lowest of its 128
// windows. No thread waits for another between the tables and the final minimum; the old kernel made every line on
// 256 lattice columns, filtered every row along z, and met twice per four rows.
// The three steps are plain functions so that a host program can run them thread by thread (stats/look2host.cpp).
// build: nvcc -O3 -arch=sm_89 -c gpulook2.cu -o gpulook2.o
#include <cstdint>
#include <cstdio>
#include <cmath>
#ifdef __CUDACC__
#include <cuda_runtime.h>
#define LOOK_FN __device__ __forceinline__
#else
#define LOOK_FN static inline
#endif
#include "gpuscan.h"

static const int LOOK_LINES = 2 * 127 + 7; // lattice lines along x that the 128 window rows touch (row m: lines 2 m .. 2 m + 6)
struct LookShared { float table[2][4][256]; unsigned char turn[2][4][264]; float mins[256]; };

// step 1, thread t (stack t >> 7, column t & 127): its share of the stack's tables and of the lines' turns
LOOK_FN void lookTables(const LookSeed &seed, LookShared &s, int t) {
    static const float GY[16] = {1, 1, -1, -1, 0, 0, 0, 0, 1, -1, 1, -1, 1, -1, 1, -1};
    const int stack = t >> 7, column = t & 127; const LookLayer &L = seed.layer[stack]; const int planes = L.planes;
    for (int j = column; j < 256; j += 128) {
        float a = 0;
        for (int r = 0; r < 7; r++) a += L.kz[r] * GY[L.d[(j + r) & 255] & 15];
        for (int k = 0; k < planes; k++) s.table[stack][k][j] = L.wyB[k] * a;
    }
    for (int line = column; line < LOOK_LINES; line += 128) {
        const unsigned char dx = L.d[(L.baseX + line) & 255];
        for (int k = 0; k < planes; k++) s.turn[stack][k][line] = (unsigned char) (L.d[(unsigned char) (dx + (unsigned char) (L.y0 + k))] + L.baseZ);
    }
}
// step 2, thread t: the lowest window of its column (in the old kernel's units: before x norm / 16)
LOOK_FN float lookColumn(const LookSeed &seed, const LookShared &s, int t) {
    const int stack = t >> 7, c0 = 2 * (t & 127); const LookLayer &L = seed.layer[stack]; const int planes = L.planes;
    float kx[7]; for (int r = 0; r < 7; r++) kx[r] = L.kx[r];
    float ring[8], lowest = 1e9f;
    auto line = [&](int i) { float v = 0; for (int k = 0; k < planes; k++) v += s.table[stack][k][(s.turn[stack][k][i] + c0) & 255]; return v; };
    for (int i = 0; i < 5; i++) ring[i] = line(i);
    for (int m0 = 0; m0 < 128; m0 += 4) // four rows at a time: 2 m0 is a multiple of 8, so the ring's slots are constants
        for (int j = 0; j < 4; j++) {
            const int first = 2 * (m0 + j);
            ring[(2 * j + 5) & 7] = line(first + 5); ring[(2 * j + 6) & 7] = line(first + 6);
            float sum = 0;
            for (int r = 0; r < 7; r++) sum += kx[r] * ring[(2 * j + r) & 7];
            lowest = fminf(lowest, sum);
        }
    return lowest;
}
// step 3: the seed's number from the columns' lowest windows
LOOK_FN float lookTotal(const LookSeed &seed, const LookShared &s) {
    float total = 0;
    for (int stack = 0; stack < 2; stack++) { float low = s.mins[stack * 128]; for (int i = 1; i < 128; i++) low = fminf(low, s.mins[stack * 128 + i]); total += low * seed.layer[stack].norm * 0.0625f; }
    return total;
}

#ifdef __CUDACC__
__global__ void look2(const LookSeed *in, float *out) {
    __shared__ LookShared s;
    const int t = threadIdx.x; const LookSeed &seed = in[blockIdx.x];
    lookTables(seed, s, t);
    __syncthreads();
    const float lowest = lookColumn(seed, s, t);
    s.mins[t] = lowest;
    __syncthreads();
    if (t == 0) out[blockIdx.x] = lookTotal(seed, s);
}

static LookSeed *deviceIn = nullptr; static float *deviceOut = nullptr; static int capacity = 0;
int gpulook_init(int maxSeeds) {
    capacity = maxSeeds;
    if (cudaMalloc(&deviceIn, (size_t) maxSeeds * sizeof(LookSeed)) != cudaSuccess || cudaMalloc(&deviceOut, (size_t) maxSeeds * sizeof(float)) != cudaSuccess) { fprintf(stderr, "gpulook: no GPU memory for %d seeds at once\n", maxSeeds); return 1; }
    return 0;
}
int gpulook_batch(const LookSeed *in, int count, float *out) {
    if (count > capacity) return 1;
    if (cudaMemcpy(deviceIn, in, (size_t) count * sizeof(LookSeed), cudaMemcpyHostToDevice) != cudaSuccess) return 1;
    look2<<<count, 256>>>(deviceIn, deviceOut);
    if (cudaMemcpy(out, deviceOut, (size_t) count * sizeof(float), cudaMemcpyDeviceToHost) != cudaSuccess) { fprintf(stderr, "gpulook: %s\n", cudaGetErrorString(cudaGetLastError())); return 1; }
    return 0;
}
#endif
