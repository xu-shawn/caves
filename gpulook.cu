// The first coarse look of the seed gate on a GPU: for each seed the lowest tapered 1280-block window mean of octave -8
// of stack A plus that of stack B, windows every 512 blocks, from the y components of the lattice gradients alone (the
// number of cpuprelook.hpp's low8y, in single precision). A third of the work of the old prelook kernel of gputables.cu
// (one gradient component instead of three). One block of 256 threads per seed, the thread being the lattice column
// along z: the gradient sums down y of a lattice line go into a ring of 16 lines in shared memory; the window rows
// along x (weighted sums of 7 lines) are made four at a time, so the threads meet twice per four rows instead of twice
// per row; the windows along z are weighted sums of 7 columns of a row.
// build: nvcc -O3 -c gpulook.cu -o gpulook.o
#include <cstdint>
#include <cstdio>
#include <cuda_runtime.h>
#include "gpuscan.h"

static const int LRING = 16, ROWS = 4;
__global__ void look1(const LookSeed *in, float *out) {
    static const float GY[16] = {1, 1, -1, -1, 0, 0, 0, 0, 1, -1, 1, -1, 1, -1, 1, -1};
    __shared__ unsigned char d[256]; __shared__ float ring[LRING * 256], rows[ROWS * 256], mins[128];
    const int t = threadIdx.x; const LookSeed &seed = in[blockIdx.x]; float total = 0;
    for (int stack = 0; stack < 2; stack++) {
        const LookLayer &L = seed.layer[stack]; const int planes = L.planes, y0 = L.y0, baseX = L.baseX;
        float wb[4], kx[7], kz[7]; int zs[7];
        for (int k = 0; k < 4; k++) wb[k] = L.wyB[k];
        for (int r = 0; r < 7; r++) { kx[r] = L.kx[r]; kz[r] = L.kz[r]; zs[r] = (L.baseZ + 2 * t + r) & 255; } // window column t (< 128) starts at cell 2 t
        d[t] = L.d[t];
        __syncthreads();
        int have = 0; float lowest = 1e9f; // lattice lines baseX .. baseX + have - 1 are in the ring (line i in slot i mod LRING)
        for (int m0 = 0; m0 < 128; m0 += ROWS) { // window rows m0 .. m0 + ROWS - 1; row m starts at line baseX + 2 m and takes 7 lines
            for (; have < 2 * (m0 + ROWS - 1) + 7; have++) {
                const unsigned char dx = d[(baseX + have) & 255]; float sy = 0;
                for (int k = 0; k < planes; k++) { const int h = d[(unsigned char) (d[(unsigned char) (dx + (unsigned char) (y0 + k))] + t)] & 15; sy += GY[h] * wb[k]; }
                ring[(have & (LRING - 1)) * 256 + t] = sy;
            }
            for (int j = 0; j < ROWS; j++) {
                float a = 0;
                for (int r = 0; r < 7; r++) a += kx[r] * ring[((2 * (m0 + j) + r) & (LRING - 1)) * 256 + t];
                rows[j * 256 + t] = a;
            }
            __syncthreads();
            if (t < 128) for (int j = 0; j < ROWS; j++) {
                float sum = 0;
                for (int r = 0; r < 7; r++) sum += kz[r] * rows[j * 256 + zs[r]];
                lowest = fminf(lowest, sum);
            }
            __syncthreads();
        }
        if (t < 128) mins[t] = lowest;
        __syncthreads();
        if (t == 0) { float low = mins[0]; for (int i = 1; i < 128; i++) low = fminf(low, mins[i]); total += low * L.norm * 0.0625f; }
        __syncthreads();
    }
    if (t == 0) out[blockIdx.x] = total;
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
    look1<<<count, 256>>>(deviceIn, deviceOut);
    if (cudaMemcpy(out, deviceOut, (size_t) count * sizeof(float), cudaMemcpyDeviceToHost) != cudaSuccess) { fprintf(stderr, "gpulook: %s\n", cudaGetErrorString(cudaGetLastError())); return 1; }
    return 0;
}
