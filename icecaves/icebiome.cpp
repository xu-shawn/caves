// See icebiome.hpp.
// build: g++ -O3 -march=native -std=c++17 -ffp-contract=off -I ../vendor/cubiomes -c icebiome.cpp -o icebiome.o
#include "icebiome.hpp"
#include "fastnoise.hpp"
#include <algorithm>
#include <cmath>
extern "C" {
#include "biomenoise.h"
float getSpline(const Spline *sp, const float *vals);   // biomenoise.c, not in the header
}

struct IceBiome { BiomeNoise bn; DoublePerlinNoise coarse[NP_MAX]; };

IceBiome *iceNew(uint64_t seed, bool large) {
    IceBiome *b = new IceBiome; initBiomeNoise(&b->bn, MC_26_3); setBiomeSeed(&b->bn, seed, large ? 1 : 0);
    for (int p = 0; p < NP_MAX; p++) {   // for looking around: only the octaves of 64 blocks and wider
        b->coarse[p] = b->bn.climate[p];
        for (OctaveNoise *o : {&b->coarse[p].octA, &b->coarse[p].octB}) { int keep = 1; for (int i = 0; i < o->octcnt; i++) if (4.0 / o->octaves[i].lacunarity >= 64.0) keep = i + 1; o->octcnt = keep; }
    }
    return b;
}
void iceFree(IceBiome *b) { delete b; }

void iceClimate(const IceBiome *b, int qx, int qz, IceClimate *out, bool exact, bool terrain) {
    const DoublePerlinNoise *n = exact ? b->bn.climate : b->coarse; double px = qx, pz = qz;
    if (exact) { px += fastDoublePerlin(&n[NP_SHIFT], qx, 0, qz) * 4.0; pz += fastDoublePerlin(&n[NP_SHIFT], qz, qx, 0) * 4.0; }
    out->c = (float) fastDoublePerlin(&n[NP_CONTINENTALNESS], px, 0, pz); out->e = (float) fastDoublePerlin(&n[NP_EROSION], px, 0, pz);
    out->w = (float) fastDoublePerlin(&n[NP_WEIRDNESS], px, 0, pz);
    out->t = (float) fastDoublePerlin(&n[NP_TEMPERATURE], px, 0, pz); out->h = (float) fastDoublePerlin(&n[NP_HUMIDITY], px, 0, pz);
    out->off = 0;
    if (terrain) { const float par[4] = {out->c, out->e, -(fabsf(fabsf(out->w) - 0.6666667F) - 0.33333334F) * 3.0F, out->w}; out->off = getSpline(b->bn.sp, par) + 0.015F; }
}

// The boxes (units of 1/10000), in the order temperature, humidity, continentalness, erosion, depth, weirdness.
struct Box { int64_t lo[6], hi[6]; };
static const int64_t F0 = -10000, F1 = 10000;
static const Box ICE = {{-10000, -10000, -10500, F0, 2000, 5000}, {-8000, 7000, -1900, F1, 9000, 11000}};
static const Box LUSH = {{F0, 7000, F0, F0, 2000, F0}, {F1, 10000, F1, F1, 9000, F1}};
static const Box DRIPSTONE = {{F0, -10000, 8000, F0, 2000, F0}, {F1, 7000, 10000, F1, 9000, F1}};
static const Box SULFUR = {{F0, -10000, -1900, 4500, 2000, -11000}, {F1, 7000, 5500, F1, 9000, -8500}};
static const Box DEEP_DARK = {{F0, F0, F0, F0, 11000, F0}, {F1, F1, F1, -3750, 11000, F1}};
static const Box SURFACE = {{F0, F0, -12000, F0, 0, F0}, {F1, F1, F1, F1, 0, F1}};   // what the surface biomes fill together; their depth is 0 or 1
static inline int64_t fixed(float v) { return (int64_t) (10000.0F * v); }
static inline int64_t outside(int64_t v, int64_t lo, int64_t hi) { const int64_t d = v > hi ? v - hi : v < lo ? lo - v : 0; return d * d; }
static inline int64_t flat(const Box &b, const int64_t np[6]) { int64_t s = 0; for (int i : {0, 1, 2, 3, 5}) s += outside(np[i], b.lo[i], b.hi[i]); return s; }

int64_t iceTemperature(const IceBiome *b, int qx, int qz) { return fixed((float) fastDoublePerlin(&b->coarse[NP_TEMPERATURE], qx, 0, qz)); }
int64_t iceBoxDistance(const IceClimate *c) { const int64_t np[6] = {fixed(c->t), fixed(c->h), fixed(c->c), fixed(c->e), 0, fixed(c->w)}; return flat(ICE, np); }

bool iceAt(const IceClimate *c, int qy, int ties) {
    const float depth = (float) (1.0 - (qy * 4) / 128.0 - 83.0 / 160.0 + c->off);
    const int64_t np[6] = {fixed(c->t), fixed(c->h), fixed(c->c), fixed(c->e), fixed(depth), fixed(c->w)}; const int64_t d = np[4];
    const int64_t ice = flat(ICE, np) + outside(d, 2000, 9000);
    const int64_t cave = std::min(flat(LUSH, np), std::min(flat(DRIPSTONE, np), flat(SULFUR, np))) + outside(d, 2000, 9000);
    const int64_t other = std::min(std::min(cave, flat(DEEP_DARK, np) + (d - 11000) * (d - 11000)), flat(SURFACE, np) + std::min(d * d, (d - 10000) * (d - 10000)));
    return ties ? ice <= other : ice < other;
}
void iceColumn(const IceClimate *c, uint64_t out[2], int ties) {
    out[0] = out[1] = 0;
    for (int qy = -16; qy < 64; qy++) if (iceAt(c, qy, ties)) out[(qy + 16) >> 6] |= 1ULL << ((qy + 16) & 63);
}
