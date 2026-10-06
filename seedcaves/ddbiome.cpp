// See ddbiome.hpp. ddColumn + ddBiomeAt are cubiomes' sampleBiomeNoise (biomenoise.c) split in two: the part that
// only depends on x, z and the part that depends on y (the depth). selftest in ddbox.cpp checks they agree.
// build: g++ -O2 -std=c++17 -ffp-contract=off -I ../vendor/cubiomes -c ddbiome.cpp -o ddbiome.o
#include "ddbiome.hpp"
#include <cmath>
#include <cstdlib>
extern "C" {
#include "biomenoise.h"
#include "layers.h"
float getSpline(const Spline *sp, const float *vals); // biomenoise.c, not in the header
}

struct DDBiome { BiomeNoise bn; uint64_t sha; };

DDBiome *ddNew(uint64_t seed) {
    DDBiome *b = new DDBiome;
    initBiomeNoise(&b->bn, MC_26_3); setBiomeSeed(&b->bn, seed, getenv("CAVE_NORMAL") ? 0 : 1); /* seedcaves: large biomes unless CAVE_NORMAL is set */ b->sha = getVoronoiSHA(seed);
    return b;
}
void ddFree(DDBiome *b) { delete b; }

void ddColumn(const DDBiome *b, int x, int z, DDClimate *out) {
    const BiomeNoise *bn = &b->bn;
    double px = x, pz = z;
    px += sampleDoublePerlin(&bn->climate[NP_SHIFT], x, 0, z) * 4.0;
    pz += sampleDoublePerlin(&bn->climate[NP_SHIFT], z, x, 0) * 4.0;
    float c = sampleDoublePerlin(&bn->climate[NP_CONTINENTALNESS], px, 0, pz);
    float e = sampleDoublePerlin(&bn->climate[NP_EROSION], px, 0, pz);
    float w = sampleDoublePerlin(&bn->climate[NP_WEIRDNESS], px, 0, pz);
    float np_param[] = { c, e, -3.0F * (fabsf(fabsf(w) - 0.6666667F) - 0.33333334F), w };
    double off = getSpline(bn->sp, np_param) + 0.015F;
    out->t = sampleDoublePerlin(&bn->climate[NP_TEMPERATURE], px, 0, pz);
    out->h = sampleDoublePerlin(&bn->climate[NP_HUMIDITY], px, 0, pz);
    out->c = c; out->e = e; out->w = w; out->off = off;
}

static int depthBump = 0;
void ddSetDepthBump(int units) { depthBump = units; }

int ddBiomeAt(const DDClimate *cc, int y) {
    float d = 1.0 - (y * 4) / 128.0 - 83.0 / 160.0 + cc->off;
    int64_t np[6];
    np[0] = (int64_t) (10000.0F * cc->t);
    np[1] = (int64_t) (10000.0F * cc->h);
    np[2] = (int64_t) (10000.0F * cc->c);
    np[3] = (int64_t) (10000.0F * cc->e);
    np[4] = (int64_t) (10000.0F * d) + depthBump;
    np[5] = (int64_t) (10000.0F * cc->w);
    return climateToBiome(MC_26_3, (const uint64_t *) np, NULL);
}

int ddBiomeDirect(const DDBiome *b, int qx, int qy, int qz) { return sampleBiomeNoise(&b->bn, NULL, qx, qy, qz, NULL, 0); }

void ddVoronoi(const DDBiome *b, int x, int y, int z, int *qx, int *qy, int *qz) { voronoiAccess3D(b->sha, x, y, z, qx, qy, qz); }

const char *ddName(int id) {
    switch (id) {
    case ocean: return "ocean"; case plains: return "plains"; case desert: return "desert"; case mountains: return "windswept_hills";
    case forest: return "forest"; case taiga: return "taiga"; case swamp: return "swamp"; case river: return "river";
    case frozen_ocean: return "frozen_ocean"; case frozen_river: return "frozen_river"; case snowy_tundra: return "snowy_plains";
    case mushroom_fields: return "mushroom_fields"; case beach: return "beach"; case jungle: return "jungle";
    case jungle_edge: return "sparse_jungle"; case deep_ocean: return "deep_ocean"; case stone_shore: return "stony_shore";
    case snowy_beach: return "snowy_beach"; case birch_forest: return "birch_forest"; case dark_forest: return "dark_forest";
    case snowy_taiga: return "snowy_taiga"; case giant_tree_taiga: return "old_growth_pine_taiga"; case wooded_mountains: return "windswept_forest";
    case savanna: return "savanna"; case savanna_plateau: return "savanna_plateau"; case badlands: return "badlands";
    case wooded_badlands_plateau: return "wooded_badlands"; case warm_ocean: return "warm_ocean"; case lukewarm_ocean: return "lukewarm_ocean";
    case cold_ocean: return "cold_ocean"; case deep_lukewarm_ocean: return "deep_lukewarm_ocean"; case deep_cold_ocean: return "deep_cold_ocean";
    case deep_frozen_ocean: return "deep_frozen_ocean"; case sunflower_plains: return "sunflower_plains"; case gravelly_mountains: return "windswept_gravelly_hills";
    case flower_forest: return "flower_forest"; case ice_spikes: return "ice_spikes"; case tall_birch_forest: return "old_growth_birch_forest";
    case giant_spruce_taiga: return "old_growth_spruce_taiga"; case shattered_savanna: return "windswept_savanna"; case eroded_badlands: return "eroded_badlands";
    case bamboo_jungle: return "bamboo_jungle"; case dripstone_caves: return "dripstone_caves"; case lush_caves: return "lush_caves";
    case meadow: return "meadow"; case grove: return "grove"; case snowy_slopes: return "snowy_slopes"; case jagged_peaks: return "jagged_peaks";
    case frozen_peaks: return "frozen_peaks"; case stony_peaks: return "stony_peaks"; case deep_dark: return "deep_dark";
    case mangrove_swamp: return "mangrove_swamp"; case cherry_grove: return "cherry_grove"; case pale_garden: return "pale_garden";
    case sulfur_caves: return "sulfur_caves"; case dappled_forest: return "dappled_forest";
    default: return "?";
    }
}
