// The exact terrain generator's density functions on a GPU: the five interpolated functions of the game's noise router
// at the cell corners of a column (every 8 blocks of height), for many columns at once. This is 95% of the time of an
// exact cavern measurement. The functions are the generated ones (density_gen.hpp), compiled for the device with the
// noise types of gamegen_dev.hpp; one thread per column, as gamegen.hpp's cornerColumn.
// The generated functions are kept as calls (__noinline__): inlined, each of the 320 is copied into every place that
// uses it and the kernel grows to megabytes (the driver then took over a quarter of an hour to load it).
// build: python3 gen_density_dev.py && nvcc -O3 --fmad=false -arch=sm_89 -c gpudensity.cu -o gpudensity.o     (gen_density_dev.py: density_gen.hpp for the device; -arch: the card's own, so nothing is compiled at load)
#include <cstdio>
#include <cuda_runtime.h>
__host__ __device__ static inline float gmin(float a, float b) { return b < a ? b : a; }
__host__ __device__ static inline float gmax(float a, float b) { return a < b ? b : a; }
#include "density_dev.hpp"
#include "gpudensity.h"

// Three kinds of thread, so that none runs long and the double-precision arithmetic (which this card does at a
// sixty-fourth of its single-precision speed, and which was 95% of the kernel's time) is done once and not per sample:
//   levelLattice   per seed, when it comes to the device: for every level and every layer of every sampling site, the y
//                  cell and place inside it
//   columnCaches   per column: the 19 functions that do not depend on the height (continents, erosion, ridges, the
//                  terrain splines; the names are those of density_gen.hpp's "(*p.col)" slots), and the x and z cell
//                  and place of every layer of every site
//   cornerLevels   per column and level: the five corner values, with the noise samples read from those lists
// The double-precision steps are the ones of GPerlin::get and getSmeared, moved; the values stay bit for bit the CPU's.
// worlds: the seeds on the device (a request batch mixes columns of several seeds); slot[i]: the world of column i
static const struct { int noise; double a, b, c; } SITE_LIST[] = {
#include "density_sites.inc"
};
static const int SITE_COUNT = sizeof(SITE_LIST) / sizeof(SITE_LIST[0]);
__global__ void levelLattice(const World *world, const Sites *sites, YL *out) { // out[level * total + entry]
    const int iy = blockIdx.x * blockDim.x + threadIdx.x; if (iy >= DENSITY_LEVELS) return;
    const World &w = *world; const Sites &s = *sites; const int y = DENSITY_MIN_Y + 8 * iy; YL *row = out + (size_t) iy * s.total;
    for (int k = 0; k < s.count; k++) {
        const GNoise &n = w.noise[s.noise[k]];
        for (int i = 0; i < n.count; i++) { const GLayer &l = n.layers[i]; const double v = wrapCoordinate((y * s.b[k]) * l.frequency) + l.perlin.offsetY; const int fy = floorInt(v); YL &e = row[s.first[k] + i]; e.fy = fy & 255; e.ry = (float) (v - fy); e.original = e.ry; }
    }
    for (int stack = 0; stack < 3; stack++) { // SmearedPerlinNoise.get's y
        const GSmearedStack &st = stack == 0 ? w.blended.mainNoise : (stack == 1 ? w.blended.minLimit : w.blended.maxLimit); const double scaled = y * (stack == 0 ? w.blended.yMain : w.blended.yMultiplier);
        for (int i = 0; i < st.count; i++) {
            const GLayer &l = st.layers[i]; const double _y = scaled * l.frequency, v = wrapCoordinate(_y) + l.perlin.offsetY; const int fy = floorInt(v); const double relativeY = v - fy;
            const double limit = (_y >= 0.0 && _y < relativeY) ? _y : relativeY, fudge = floorInt(limit / l.perlin.fudgeYScale + 1.0000000116860974E-7) * l.perlin.fudgeYScale;
            YL &e = row[s.first[s.count + stack] + i]; e.fy = fy & 255; e.ry = (float) (relativeY - fudge); e.original = (float) relativeY;
        }
    }
}
__global__ void columnCaches(const World *worlds, const Sites *sites, const int *xz, const int *slot, int count, Col *cols, XZ *lattice) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x; if (i >= count) return;
    const World &w = worlds[slot[i]]; const Sites &s = *sites;
    Col col; col.clear(); P p; p.w = &w; p.x = xz[2 * i]; p.z = xz[2 * i + 1]; p.col = &col; p.sites = sites; p.xz = NULL; p.yl = NULL; p.at(0);
    df_24_overworld_offset(p); df_26_cache(p); df_35_overworld_ridges_folded(p); df_41_overworld_ridges(p); df_43_cache(p); df_45_shift_x(p); df_47_cache(p); df_49_shift_z(p); df_51_cache(p); df_59_overworld_erosion(p);
    df_61_cache(p); df_107_overworld_continents(p); df_109_cache(p); df_112_cache(p); df_114_overworld_jaggedness(p); df_116_cache(p); df_138_overworld_factor(p); df_140_cache(p); df_292_cache(p);
    cols[i] = col;
    XZ *row = lattice + (size_t) i * s.total;
    auto place = [&](const GLayer &l, double x, double z, XZ &e) { // GNoise::get and GPerlin::get up to the lattice split
        const double vx = wrapCoordinate(x * l.frequency) + l.perlin.offsetX, vz = wrapCoordinate(z * l.frequency) + l.perlin.offsetZ; const int fx = floorInt(vx), fz = floorInt(vz);
        e.fx = (uint8_t) (fx & 255); e.fz = (uint8_t) (fz & 255); e.rx = (float) (vx - fx); e.rz = (float) (vz - fz);
    };
    for (int k = 0; k < s.count; k++) { const GNoise &n = w.noise[s.noise[k]]; const double x = p.x * s.a[k], z = p.z * s.c[k]; for (int j = 0; j < n.count; j++) place(n.layers[j], x, z, row[s.first[k] + j]); }
    for (int stack = 0; stack < 3; stack++) {
        const GSmearedStack &st = stack == 0 ? w.blended.mainNoise : (stack == 1 ? w.blended.minLimit : w.blended.maxLimit); const double scale = stack == 0 ? w.blended.xzMain : w.blended.xzMultiplier, x = p.x * scale, z = p.z * scale;
        for (int j = 0; j < st.count; j++) place(st.layers[j], x, z, row[s.first[s.count + stack] + j]);
    }
}
__global__ void cornerLevels(const World *worlds, const Sites *sites, const int *xz, const int *slot, int count, Col *cols, const XZ *lattice, const YL *levels, float *out) {
    const int t = blockIdx.x * blockDim.x + threadIdx.x, i = t / DENSITY_LEVELS, iy = t % DENSITY_LEVELS; if (i >= count) return;
    P p; p.w = worlds + slot[i]; p.x = xz[2 * i]; p.z = xz[2 * i + 1]; p.col = cols + i; p.sites = sites; p.xz = lattice + (size_t) i * sites->total; p.yl = levels + ((size_t) slot[i] * DENSITY_LEVELS + iy) * sites->total;
    p.at(DENSITY_MIN_Y + 8 * iy);
    float *column = out + (size_t) i * DENSITY_VALUES;
    column[0 * DENSITY_LEVELS + iy] = corner_0(p); column[1 * DENSITY_LEVELS + iy] = corner_1(p); column[2 * DENSITY_LEVELS + iy] = corner_2(p);
    column[3 * DENSITY_LEVELS + iy] = corner_3(p); column[4 * DENSITY_LEVELS + iy] = corner_4(p);
}

static World *dWorlds; static int *dXZ, *dSlot; static Col *dCols; static float *dOut, *hostOut; static int capacity = 0, worldSlots = 0;
static Sites hostSites; static Sites *dSites; static XZ *dLattice; static YL *dLevels;
int gpudensity_init(int maxColumns, int slots) {
    capacity = maxColumns; worldSlots = slots;
    if (cudaMalloc(&dWorlds, (size_t) slots * sizeof(World)) != cudaSuccess || cudaMalloc(&dXZ, (size_t) maxColumns * 2 * sizeof(int)) != cudaSuccess || cudaMalloc(&dSlot, (size_t) maxColumns * sizeof(int)) != cudaSuccess || cudaMalloc(&dCols, (size_t) maxColumns * sizeof(Col)) != cudaSuccess ||
        cudaMalloc(&dSites, sizeof(Sites)) != cudaSuccess || cudaMalloc(&dOut, (size_t) maxColumns * DENSITY_VALUES * sizeof(float)) != cudaSuccess || cudaHostAlloc(&hostOut, (size_t) maxColumns * DENSITY_VALUES * sizeof(float), cudaHostAllocDefault) != cudaSuccess) { fprintf(stderr, "gpudensity: no memory\n"); return 1; }
    if (cudaDeviceSetLimit(cudaLimitStackSize, 16384) != cudaSuccess) fprintf(stderr, "gpudensity: the stack limit was not set\n");
    hostSites.count = 0; // the lists' layout comes with the first world (the layer counts are the same for every seed)
    return 0;
}
int gpudensity_world(int slot, const void *world, size_t bytes) {
    if (slot < 0 || slot >= worldSlots || bytes != sizeof(World)) return 1;
    const World &w = *(const World *) world;
    if (hostSites.count == 0) {
        if (SITE_COUNT > GEN_MAX_SITES) { fprintf(stderr, "gpudensity: %d sites, room for %d\n", SITE_COUNT, GEN_MAX_SITES); return 1; }
        int at = 0; hostSites.count = SITE_COUNT;
        for (int k = 0; k < SITE_COUNT; k++) { hostSites.noise[k] = SITE_LIST[k].noise; hostSites.a[k] = SITE_LIST[k].a; hostSites.b[k] = SITE_LIST[k].b; hostSites.c[k] = SITE_LIST[k].c; hostSites.first[k] = at; at += w.noise[SITE_LIST[k].noise].count; }
        hostSites.first[SITE_COUNT] = at; at += w.blended.mainNoise.count; hostSites.first[SITE_COUNT + 1] = at; at += w.blended.minLimit.count; hostSites.first[SITE_COUNT + 2] = at; at += w.blended.maxLimit.count; hostSites.first[SITE_COUNT + 3] = at; hostSites.total = at;
        if (cudaMemcpy(dSites, &hostSites, sizeof(Sites), cudaMemcpyHostToDevice) != cudaSuccess || cudaMalloc(&dLattice, (size_t) capacity * at * sizeof(XZ)) != cudaSuccess || cudaMalloc(&dLevels, (size_t) worldSlots * DENSITY_LEVELS * at * sizeof(YL)) != cudaSuccess) { fprintf(stderr, "gpudensity: no memory for the lattice lists (%d entries a column)\n", at); return 1; }
        fprintf(stderr, "gpudensity: %d sites, %d layers in all: %zu MB of x-z places for %d columns\n", SITE_COUNT, at, (size_t) capacity * at * sizeof(XZ) >> 20, capacity);
    }
    if (cudaMemcpy(dWorlds + slot, world, sizeof(World), cudaMemcpyHostToDevice) != cudaSuccess) return 1;
    levelLattice<<<1, 64>>>(dWorlds + slot, dSites, dLevels + (size_t) slot * DENSITY_LEVELS * hostSites.total);
    return cudaDeviceSynchronize() != cudaSuccess;
}
// the columns' values, in a buffer that stays valid until the next call (NULL: failed)
const float *gpudensity_run(const int *xz, const int *slot, int count) {
    if (count > capacity || count <= 0 || hostSites.count == 0) return NULL;
    if (cudaMemcpy(dXZ, xz, (size_t) count * 2 * sizeof(int), cudaMemcpyHostToDevice) != cudaSuccess || cudaMemcpy(dSlot, slot, (size_t) count * sizeof(int), cudaMemcpyHostToDevice) != cudaSuccess) return NULL;
    columnCaches<<<(count + 63) / 64, 64>>>(dWorlds, dSites, dXZ, dSlot, count, dCols, dLattice);
    cornerLevels<<<(count * DENSITY_LEVELS + 127) / 128, 128>>>(dWorlds, dSites, dXZ, dSlot, count, dCols, dLattice, dLevels, dOut);
    if (cudaMemcpy(hostOut, dOut, (size_t) count * DENSITY_VALUES * sizeof(float), cudaMemcpyDeviceToHost) != cudaSuccess) { fprintf(stderr, "gpudensity: %s\n", cudaGetErrorString(cudaGetLastError())); return NULL; }
    return hostOut;
}
size_t gpudensity_world_size() { return sizeof(World); }
