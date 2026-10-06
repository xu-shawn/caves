// interface of the GPU parts of the seed gate (gpuscan.cu: the phase scan; gputables.cu: the first look at the tables)
#pragma once
#include <cstdint>
struct GpuScanWindows {
    uint64_t cheeseLo, cheeseHi, o8Lo, o8Hi, o7Lo, o7Hi; // the name hashes of minecraft:cave_cheese, octave_-8, octave_-7
    int64_t low[4], high[4];                             // the windows of octave -8 A, -7 A, -8 B, -7 B in units of 2^-45 of a cell (exclusive)
    // the terrain score on the GPU too (terrain = 1): the name hashes of minecraft:erosion, continentalness, ridge and of
    // octave_-9 .. octave_-5; a seed passes if its score is at most terrainCut (single precision: the host sets the cut a
    // little loose and redoes the score exactly)
    uint64_t noiseLo[3], noiseHi[3], octLo[5], octHi[5]; float terrainCut; int terrain;
};
int gpuscan_init(const GpuScanWindows *w, uint32_t capacity);
long gpuscan_batch(uint64_t base, uint32_t threads, int perThread, uint64_t *out);
long gpuscan_chain_batch(uint64_t base, uint32_t threads, int perThread, uint64_t *out);   // the same over indexes of the chain: seed = (index x 0x9e3779b97f4a7c15) ^ 0x6a09e667f3bcc909 (one mix per seed instead of two)

// one of the four lowest cave_cheese layers of a seed, ready for the GPU: its permutation table, and the weights of
// analyticgate.hpp for the heights (y0, planes, wyA, wyB), for a 512-block window along x and along z (the first
// lattice line and a weight pair per line), and the layer's amplitude over the window's volume
struct TableLayer { uint8_t d[256]; int32_t y0, planes, baseX, baseZ; float wyA[4], wyB[4], kxA[6], kxB[6], kzA[6], kzB[6], norm; };
struct TableSeed { TableLayer layer[4]; };               // octave -8 A, -7 A, -8 B, -7 B
int gputables_init(int maxSeeds, int maxPrelook = 0);
int gpuprelook_batch(const TableSeed *in, int count, float *out);   // the cheap first number (octave -8 alone, coarse): see gputables.cu
int gputables_batch(const TableSeed *in, int count, float *out);
void gputables_skip(float level);

// the first coarse look of the gate on the GPU (gpulook.cu): octave -8 of each stack with the y weights and the 7-tap
// kernels of the tapered 1280-block window along x and z (cpuprelook.hpp's low8y, in single precision)
struct LookLayer { uint8_t d[256]; int32_t y0, planes, baseX, baseZ; float wyB[4], kx[7], kz[7], norm; };
struct LookSeed { LookLayer layer[2]; };                 // stack A, stack B
int gpulook_init(int maxSeeds);
int gpulook_batch(const LookSeed *in, int count, float *out);   // out[i]: the lowest window mean of stack A plus that of stack B
