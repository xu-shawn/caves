// icecaves/icepairs.cpp: the cave project's per-seed scanner (../seedcaves/ringpairs.cpp, below) for ICE CAVES.
// The trick is unchanged: the cave noise of a whole world is two small tables, and the places where both stacks of the
// cheese noise are low are listed without visiting the world. What changes is what a place needs:
//   - the pairs are picked on the cheese of the five lowest heights (y -40 .. 24): an ice cavern is a low slab;
//   - a place is kept when the climate is inside the ice caves box at one of the 16 quarter centres of its window
//     (temperature first), not when the ground over it is high;
//   - on the fine grid a point counts as air when it is open, under the floor (the rough surface there) and dry by the
//     aquifers' rule for that depth under a sea floor; clusters are counted whole and by their points in ice caves cells
//     (icebiome.hpp at the quarter's climate).
// Output: "SEED X Z ice N of M pure P floor F points N quarters Q ice_at X Z pure_at X Z": the cluster with the most ice
// caves points (N of its M), the largest cluster that is ice caves throughout, the mean rough surface, the air points,
// the quarters in the box, and the middle column of each of the two clusters.
// Places whose clusters are large enough (CLOSER) get the ice caves column by column instead of by quarters.
// build: g++ -O3 -march=native -std=c++17 -ffp-contract=off -I ../seedcaves -I ../vendor/cubiomes icepairs.cpp icebiome.o ../vendor/cub_*.o -lpthread -o icepairs
// usage: icepairs SEED [PAIRS=3000000] [TOP=40] [THREADS=12] [PLACES=4000]
// ---------------------------------------------------------------------------------------------------------------------
// seedcaves/ringpairs.cpp: cavepairs3 for ONE seed and a ring of distances from the origin, in the large-biomes preset.
//   RING_IN, RING_OUT (blocks): only windows whose centre is at that distance from (0, 0) (default: the whole world)
//   DIRECT=1: the ring's windows are listed one by one from the tables (window means of every world position in the ring;
//             for rings up to about 8 million blocks) and each has its one place; without it the pairs are picked over the
//             whole world as in cavepairs3 and only their places in the ring are kept (for the outermost ring)
//   SURFACE_MIN (default 72): the mean rough surface a place needs; 0 near the origin, where nothing should be left out
//   MIN_POINTS (default 1000): fine points a window needs for its clusters to be made
//   CAVE_NORMAL=1: the normal overworld's terrain (cubiomes large = 0)
// Output: "seed x z cluster open surface points free": free = the most points outside the deep dark that one cluster has
// (deep dark estimate: erosion under -0.375 and more than 134.4 blocks under the surface estimate, where depth passes 1.05).
// The best TOP by cluster and the best TOP by free are printed (a window once).
// Stage 1 of the cave search, for a whole world at once.
//
// The trick. A game noise is the sum of two Perlin stacks, the second sampled at coordinates stretched by 337/331
// (NormalNoise.INPUT_FACTOR), and a Perlin octave repeats every 256 lattice cells. So for the cave noises (lowest octave
// -8, 256 blocks a cell) the first stack ("A") repeats every 65,536 blocks and the second ("B") every 65,536 * 331/337.
// On a grid of 65,536 / 1011 = 64.82 blocks, A repeats every 1011 steps and B every 993, exactly. A world position is a
// pair (A index, B index) per axis, and as the grid index runs over the world every pair with the same remainder mod 3
// (gcd(1011, 993) = 3) turns up, every 334,641 steps (21.7 million blocks). So the cave noise of a whole world is two
// small tables, and the places where both stacks are low can be listed without visiting the world:
//   1. tables of the A and B parts of cave_cheese, cave_layer and the aquifer floodedness on the grid, 16 heights
//   2. the mean cheese of every 8 x 8 step window (518 blocks) and of every 16 x 16 step window (1037 blocks), for A
//      and for B; the mean of a world window is the sum of its A mean and its B mean, so the pairs with the lowest sums
//      come from the sorted lists (half of the pairs from each window size)
//   3. for those pairs the cheese-cave formula from the tables (open if clamp(0.27 + cheese, -1, 1) + 4 layer^2 < 0, dry
//      if floodedness <= 0.4), counted per height over the 16 x 16 steps around the window's centre
//   4. each pair is at 2 or 3 places per axis in the world (the Chinese remainder theorem gives them) with the same
//      cave noise but different terrain, and terrain decides: a big cavern needs a high surface above it, and only one
//      place in eight has that. So every place of every pair gets a surface estimate (cubiomes' depth; 4 samples, the
//      other 12 only if those are high enough), the low ones go, and the rest are ranked by the pair's open points
//      under their surface.
//   5. the cheese noise has as much in its 64-block octave as in the 256 and 128 ones, and the 64.8-block grid cannot
//      see it; so for the best places the tables are made again where their windows are, at half the step (32.4 blocks)
//      and 33 heights (y -56 + 8 l): a few percent of the table area. The score of a place is its largest cluster of
//      open-and-dry fine points under the surface that touch (6 neighbours): the biggest caverns are the ones that hang
//      together, not the windows with the most air. This is close to what stage 2 computes on its 16-block grid.
// Prints the best windows: "seed x z cluster open surface points" (x, z = window centre; cluster = fine points in the
// largest cluster at least MARGIN blocks under the surface, of 32 x 32 columns x 32 heights over 1037 x 1037 blocks, 8,404
// blocks a point; open = the pair's open-and-dry points on the coarse grid, no surface limit; points = fine ones under
// the surface).
//
// build: g++ -O3 -march=native -std=c++17 -I vendor/cubiomes cavepairs.cpp vendor/cub_*.o -lpthread -o cavepairs
// usage: cavepairs3 SEED [PAIRS=600000] [TOP=300] [THREADS=8] [PLACES=15000] [MIN_CUT=0]     (STATS=1: only the tables' statistics)
// PLACES: how many places (the best by open points under the surface) get the fine tables and the cluster.
// MIN_CUT: stop early (no sites) if the cut of the terrain step, known a third of the way through, is lower.
#include "cavenoise.hpp"
#include "icebiome.hpp"
extern "C" {
#include "biomenoise.h"
float getSpline(const Spline *sp, const float *vals);
}
#include <immintrin.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <thread>
#include <unordered_set>
#include <vector>
#include <memory>

static const int NA = 1011, NB = 993, LEVELS = 16, WINDOW = 16, MARGIN = 48;
static int MEAN_LEVELS = 5;   // env MEAN_LEVELS   // (ice: the pairs are picked on y -40 .. 24)
static const int FINE_MADE = 18, FINE_Y0 = -53;  // fine heights that are made for a place under ice caves: y -53 (the lowest air over the lava) .. 83
static inline int fineY(int level) { return FINE_Y0 + 8 * level; }
static const int ICE_TOP = 24;   // coarse heights up to here count for a place before the fine look
static const int FINE_LEVELS = 33, FINE = 2 * WINDOW;   // the fine grid: half steps, y = FINE_Y0 + 8 l
static int COARSE_OCTAVES = 4;                            // cheese octaves in the coarse tables (of 7: the three highest are 2% of the variance; env COARSE_OCTAVES)
static const int CUT_PLACES = 15000;                      // the early exit's cut is the open points of the place at 1.5 x this rank
static const int BASE_OCTAVES = 2;                        // of those, the ones every column gets (the pairs are picked on them)
static const int MADE_LEVELS = 5;                         // coarse heights that are made (y -40 .. 24): ice caverns end under the sea floor; the fine tables have what is above
static const double SURFACE_FIRST = 64, SURFACE_MEAN = 72; // a place goes on if 4 rough surface samples average the first, and all 16 the second
static const int COMMON = 334641;                 // lcm(1011, 993): the cave noise repeats every that many steps
static const double STEP = 65536.0 / NA;
static const double BORDER = 29990000.0;
static inline int levelY(int level) { return -40 + 16 * level; }

// The surface estimate at a quart position: cubiomes' depth (the offset spline of continentalness, erosion and ridges)
// with only the first octaves of the three noises and without the small shift of the sampling position. With 5, 3, 3
// octaves the mean over a window is within a block of the full estimate at under half the cost; with 3, 2, 2 it is
// within 2 blocks at under a third.
static inline double firstOctaves(const DoublePerlinNoise *n, double x, double z, int k) {
    const double f = 337.0 / 331.0; double v = 0;
    for (int i = 0; i < k && i < n->octA.octcnt; i++) { const PerlinNoise *p = n->octA.octaves + i; v += p->amplitude * fastPerlin(p, x * p->lacunarity, 0, z * p->lacunarity); }
    for (int i = 0; i < k && i < n->octB.octcnt; i++) { const PerlinNoise *p = n->octB.octaves + i; v += p->amplitude * fastPerlin(p, x * f * p->lacunarity, 0, z * f * p->lacunarity); }
    return v * n->amplitude;
}
static inline double surfaceEstimate(const BiomeNoise *bn, int qx, int qz, int kc, int ke, int kw) {
    float c = (float) firstOctaves(&bn->climate[NP_CONTINENTALNESS], qx, qz, kc), e = (float) firstOctaves(&bn->climate[NP_EROSION], qx, qz, ke), w = (float) firstOctaves(&bn->climate[NP_WEIRDNESS], qx, qz, kw);
    float p[] = {c, e, -3.0F * (fabsf(fabsf(w) - 0.6666667F) - 0.33333334F), w};
    return 128.0 * (1.0 - 83.0 / 160.0 + getSpline(bn->sp, p) + 0.015F);
}

// fastPerlin at the octave's own y, with the value's slope along d1 and d3 (the lattice coordinates)
static inline double fastPerlinSlope(const PerlinNoise *noise, double d1, double d3, double *s1, double *s3) {
    static const double FAST_GRAD[16][3] = {{1, 1, 0}, {-1, 1, 0}, {1, -1, 0}, {-1, -1, 0}, {1, 0, 1}, {-1, 0, 1}, {1, 0, -1}, {-1, 0, -1}, {0, 1, 1}, {0, -1, 1}, {0, 1, -1}, {0, -1, -1}, {1, 1, 0}, {0, -1, 1}, {-1, 1, 0}, {0, -1, -1}};
    auto fastLerp = [](double part, double from, double to) { return from + part * (to - from); };
    const double d2 = noise->d2, t2 = noise->t2; const uint8_t h2 = noise->h2;
    d1 += noise->a; d3 += noise->c;
    const double i1 = floor(d1), i3 = floor(d3); d1 -= i1; d3 -= i3;
    const uint8_t h1 = (uint8_t) (int) i1, h3 = (uint8_t) (int) i3;
    const double t1 = d1 * d1 * d1 * (d1 * (d1 * 6.0 - 15.0) + 10.0), t3 = d3 * d3 * d3 * (d3 * (d3 * 6.0 - 15.0) + 10.0);
    const double u1 = 30.0 * d1 * d1 * (d1 - 1) * (d1 - 1), u3 = 30.0 * d3 * d3 * (d3 - 1) * (d3 - 1);
    const uint8_t *idx = noise->d;
    const uint8_t a1 = (uint8_t) (idx[h1] + h2), b1 = (uint8_t) (idx[h1 + 1] + h2);
    const uint8_t a2 = (uint8_t) (idx[a1] + h3), a3 = (uint8_t) (idx[a1 + 1] + h3), b2 = (uint8_t) (idx[b1] + h3), b3 = (uint8_t) (idx[b1 + 1] + h3);
    const double *g1 = FAST_GRAD[idx[a2] & 15], *g2 = FAST_GRAD[idx[b2] & 15], *g3 = FAST_GRAD[idx[a3] & 15], *g4 = FAST_GRAD[idx[b3] & 15],
                 *g5 = FAST_GRAD[idx[a2 + 1] & 15], *g6 = FAST_GRAD[idx[b2 + 1] & 15], *g7 = FAST_GRAD[idx[a3 + 1] & 15], *g8 = FAST_GRAD[idx[b3 + 1] & 15];
    auto dot = [](const double *g, double a, double b, double c) { return g[0] * a + g[1] * b + g[2] * c; };
    const double l1 = dot(g1, d1, d2, d3), l2 = dot(g2, d1 - 1, d2, d3), l3 = dot(g3, d1, d2 - 1, d3), l4 = dot(g4, d1 - 1, d2 - 1, d3),
                 l5 = dot(g5, d1, d2, d3 - 1), l6 = dot(g6, d1 - 1, d2, d3 - 1), l7 = dot(g7, d1, d2 - 1, d3 - 1), l8 = dot(g8, d1 - 1, d2 - 1, d3 - 1);
    const double m1 = fastLerp(t1, l1, l2), m3 = fastLerp(t1, l3, l4), m5 = fastLerp(t1, l5, l6), m7 = fastLerp(t1, l7, l8);
    const double n1 = fastLerp(t2, m1, m3), n5 = fastLerp(t2, m5, m7);
    const double x1 = fastLerp(t1, g1[0], g2[0]) + u1 * (l2 - l1), x3 = fastLerp(t1, g3[0], g4[0]) + u1 * (l4 - l3), x5 = fastLerp(t1, g5[0], g6[0]) + u1 * (l6 - l5), x7 = fastLerp(t1, g7[0], g8[0]) + u1 * (l8 - l7);
    *s1 = fastLerp(t3, fastLerp(t2, x1, x3), fastLerp(t2, x5, x7));
    const double z1 = fastLerp(t2, fastLerp(t1, g1[2], g2[2]), fastLerp(t1, g3[2], g4[2])), z5 = fastLerp(t2, fastLerp(t1, g5[2], g6[2]), fastLerp(t1, g7[2], g8[2]));
    *s3 = fastLerp(t3, z1, z5) + u3 * (n5 - n1);
    return fastLerp(t3, n1, n5);
}

// the climate at a quart position for the ice caves rule: every octave, no shift of the sampling position
static inline void climateAt(const BiomeNoise *bn, int qx, int qz, IceClimate *c, bool all) {
    c->t = (float) firstOctaves(&bn->climate[NP_TEMPERATURE], qx, qz, 99); c->h = c->c = c->e = c->w = 0; c->off = 0;
    if (!all) return;
    c->h = (float) firstOctaves(&bn->climate[NP_HUMIDITY], qx, qz, 99); c->c = (float) firstOctaves(&bn->climate[NP_CONTINENTALNESS], qx, qz, 99);
    c->e = (float) firstOctaves(&bn->climate[NP_EROSION], qx, qz, 99); c->w = (float) firstOctaves(&bn->climate[NP_WEIRDNESS], qx, qz, 99);
    float p[] = {c->c, c->e, -3.0F * (fabsf(fabsf(c->w) - 0.6666667F) - 0.33333334F), c->w}; c->off = getSpline(bn->sp, p) + 0.015F;
}

// The coarse tables, a cache line per column: the formula step reads 600,000 x 256 x 2 of them, and memory is what it
// waits for. cheese in 1/4096, layer and floodedness in 1/64 (a height that is not made: cheese +1, which is closed).
struct alignas(64) Packed { int16_t cheese[16]; int8_t layer[16], flood[16]; };
struct Tables {
    int n;
    std::vector<Packed> columns;   // [i][j]
    std::vector<float> sum;        // [i][j]: the base cheese summed over the MEAN_LEVELS lowest heights
    std::vector<float> low;        // [i][j]: the same over the three lowest (y -40, -24, -8)
    std::vector<uint8_t> complete; // [i][j]: the column has all its noises (see step 1)
    const Packed &at(int i, int j) const { return columns[(size_t) i * n + j]; }
};

// one stack of a noise (half 0: the A stack, 1: the B stack; every second layer) down a column: adds its values at
// y = y0 + step k (times the noise's y scale) for k < count to out; only the layers firstOctave .. octaves - 1 of the stack
static inline void addColumn(const NormalNoise &noise, int half, int octaves, double x, double z, double yScale, int y0, int step, int count, float *out, int firstOctave = 0) {
    for (size_t i = half + 2 * firstOctave; i < noise.layers.size() && (int) (i / 2) < octaves; i += 2) {
        const NoiseLayer &layer = noise.layers[i]; ColumnPerlin column(&layer.perlin, x * layer.frequency, z * layer.frequency);
        for (int k = 0; k < count; k++) out[k] += layer.amplitude * (float) column.at((y0 + step * k) * yScale * layer.frequency);
    }
}

template <class Body> static void parallel(int threads, int n, Body body) {
    std::atomic<int> next(0); std::vector<std::thread> pool;
    for (int t = 0; t < threads; t++) pool.emplace_back([&] { for (int i; (i = next++) < n;) body(i); });
    for (auto &t : pool) t.join();
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: cavepairs3 SEED [PAIRS=600000] [TOP=300] [THREADS=8] [PLACES=15000] [MIN_CUT=0]\n"); return 1; }
    const uint64_t seed = (uint64_t) strtoll(argv[1], 0, 10); const size_t wantPairs = (argc > 2 ? strtoull(argv[2], 0, 10) : 3000000) / 2; const int top = argc > 3 ? atoi(argv[3]) : 40, threads = argc > 4 ? atoi(argv[4]) : 12; const size_t wantPlaces = argc > 5 ? strtoull(argv[5], 0, 10) : 15000; const int minCut = argc > 6 ? atoi(argv[6]) : 0;
    if (getenv("COARSE_OCTAVES")) COARSE_OCTAVES = atoi(getenv("COARSE_OCTAVES"));
    if (getenv("MEAN_LEVELS")) MEAN_LEVELS = atoi(getenv("MEAN_LEVELS"));
    int selectA = 12, selectB = 16; if (getenv("SELECT")) { selectA = selectB = 0; sscanf(getenv("SELECT"), "%d,%d", &selectA, &selectB); }   // window sizes (steps) the pairs are picked on; one size: all pairs from it
    const int lookNear = getenv("L1_NEAR") ? atoi(getenv("L1_NEAR")) : 0;
    const int fineOctaves = getenv("FINE_OCTAVES") ? atoi(getenv("FINE_OCTAVES")) : 5;   // cheese octaves in the fine tables (of 7)
    const bool oracle = getenv("ORACLE") != NULL; const size_t wantIce = getenv("PLACES_ICE") ? strtoull(getenv("PLACES_ICE"), 0, 10) : wantPlaces / 2;   // print each place's ranks (pair by mean cheese, place by coarse open points) for tuning
    const double ringIn = getenv("RING_IN") ? atof(getenv("RING_IN")) : 0, ringOut = getenv("RING_OUT") ? atof(getenv("RING_OUT")) : 1e18, surfaceMin = getenv("SURFACE_MIN") ? atof(getenv("SURFACE_MIN")) : SURFACE_MEAN;
    const int minQuarters = getenv("MIN_QUARTERS") ? atoi(getenv("MIN_QUARTERS")) : 1;
    const bool ringed = getenv("RING_IN") || getenv("RING_OUT");
    const float cornerGate = getenv("CORNER_GATE") ? (float) atof(getenv("CORNER_GATE")) : -0.6f;
    const int closer = getenv("CLOSER") ? atoi(getenv("CLOSER")) : 100;   // fine points of a pure cluster (twice that of an ice cluster) from which a place gets the ice caves column by column
    const float tGate = getenv("T_GATE") ? (float) atof(getenv("T_GATE")) : -0.4f;
    const bool direct = getenv("DIRECT") != NULL, large = getenv("CAVE_LARGE") != NULL; const int minPoints = getenv("MIN_POINTS") ? atoi(getenv("MIN_POINTS")) : 60;
    auto inRing = [&](long gi, long gj) { double x = (gi + WINDOW / 2.0) * STEP, z = (gj + WINDOW / 2.0) * STEP, d = std::sqrt(x * x + z * z); return d >= ringIn && d < ringOut; };
    auto clock0 = std::chrono::steady_clock::now();
    auto seconds = [&] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - clock0).count(); };
    const NormalNoise cheese = makeNoise(seed, "minecraft:cave_cheese"), layer = makeNoise(seed, "minecraft:cave_layer"), flood = makeNoise(seed, "minecraft:aquifer_fluid_level_floodedness");

    // 1. tables, in two parts. For every column only the two lowest cheese octaves (BASE_OCTAVES): the window means that
    //    pick the pairs are theirs (the higher octaves average out over a window: what the 64.8-block grid adds of
    //    them is sampling noise). The other cheese octaves, cave_layer and the floodedness are made later, and only for
    //    the columns inside the windows of the pairs that get the formula (complete() below): a fifth of the columns.
    Tables table[2];
    for (int half = 0; half < 2; half++) {
        Tables &t = table[half]; t.n = half ? NB : NA;
        t.columns.resize((size_t) t.n * t.n); t.sum.resize((size_t) t.n * t.n); t.low.resize((size_t) t.n * t.n); t.complete.assign((size_t) t.n * t.n, 0);
        parallel(threads, t.n, [&](int i) {
            for (int j = 0; j < t.n; j++) {
                double x = i * STEP, z = j * STEP; Packed &p = t.columns[(size_t) i * t.n + j]; float sum = 0, low = 0, c[LEVELS] = {0};
                addColumn(cheese, half, BASE_OCTAVES, x, z, 0.6666666666666666, levelY(0), 16, MADE_LEVELS, c);
                for (int k = 0; k < LEVELS; k++) {
                    p.layer[k] = p.flood[k] = 0;
                    if (k >= MADE_LEVELS) { p.cheese[k] = 4096; continue; } // closed
                    if (k < MEAN_LEVELS) sum += c[k];
                    if (k < 3) low += c[k];
                    p.cheese[k] = (int16_t) std::lrint(std::max(-3.9f, std::min(3.9f, c[k])) * 4096);
                }
                t.sum[(size_t) i * t.n + j] = sum; t.low[(size_t) i * t.n + j] = low;
            }
        });
    }
    // 1b. the weirdness tables (the pairs are picked with them)
    BiomeNoise biomeNoise; initBiomeNoise(&biomeNoise, MC_26_3); setBiomeSeed(&biomeNoise, seed, large ? 1 : 0);
    const DoublePerlinNoise &wn = biomeNoise.climate[NP_WEIRDNESS]; std::vector<float> box[2], wide[2], ownPoint[2], widePoint[2];   // (the Point tables: the values themselves, for the first look)
    const int ownSize = getenv("OWN_SIZE") ? atoi(getenv("OWN_SIZE")) : 12; const bool ownMax = getenv("OWN_MAX") != NULL;
    // WBOX_MIN=value: the whole weirdness of a place before any climate sample. Its widest octave (-7, cells of 512 blocks)
    // repeats every TWO periods of the cave noise, so with a table over two periods (every second step: n x n again) the
    // mean weirdness over the middle of a place's window is two more lookups: own (pair) + wide (place). Places under
    // WBOX_MIN are dropped before the temperature is asked.
    const bool haveWide = getenv("WBOX_MIN") != NULL || getenv("WBOX") != NULL; const float wboxMin = getenv("WBOX_MIN") ? (float) atof(getenv("WBOX_MIN")) : -1e9f, wboxSlope = getenv("WBOX_SLOPE") ? (float) atof(getenv("WBOX_SLOPE")) : 0.0f;
    const bool ownPrune = getenv("OWN_KEEP") && !oracle;
    if (ownPrune || haveWide) {
        for (int half = 0; half < 2; half++) {
            const int n = table[half].n; std::vector<float> &own = ownPoint[half]; own.resize((size_t) n * n); const OctaveNoise &o = half ? wn.octB : wn.octA; const double f = half ? 337.0 / 331.0 : 1.0;
            parallel(threads, n, [&](int i) { for (int j = 0; j < n; j++) { double v = 0; for (int k = 1; k < o.octcnt; k++) { const PerlinNoise *q = o.octaves + k; v += q->amplitude * fastPerlin(q, i * STEP / 4 * f * q->lacunarity, 0, j * STEP / 4 * f * q->lacunarity); } own[(size_t) i * n + j] = (float) (v * wn.amplitude); } });
            // the mean over the OWN_SIZE x OWN_SIZE steps (12) about the middle of the 16 x 16 window whose origin is (i, j)
            const int size = ownSize, shift = (WINDOW - size) / 2; std::vector<double> rows((size_t) n * n); box[half].resize((size_t) n * n);
            for (int i = 0; i < n; i++) { const float *in = &own[(size_t) i * n]; double acc = 0; for (int d = 0; d < size; d++) acc += in[(shift + d) % n]; for (int j = 0; j < n; j++) { rows[(size_t) i * n + j] = acc; acc += in[(j + shift + size) % n] - in[(j + shift) % n]; } }
            for (int j = 0; j < n; j++) { double acc = 0; for (int d = 0; d < size; d++) acc += rows[(size_t) ((shift + d) % n) * n + j]; for (int i = 0; i < n; i++) { box[half][(size_t) i * n + j] = (float) (acc / (size * size)); acc += rows[(size_t) ((i + shift + size) % n) * n + j] - rows[(size_t) ((i + shift) % n) * n + j]; } }
        }
    }
    if (haveWide) for (int half = 0; half < 2; half++) {
        const int n = table[half].n, size = ownSize / 2; std::vector<float> &w = widePoint[half]; w.resize((size_t) n * n); const PerlinNoise *q = (half ? wn.octB : wn.octA).octaves; const double f = half ? 337.0 / 331.0 : 1.0;
        parallel(threads, n, [&](int i) { for (int j = 0; j < n; j++) w[(size_t) i * n + j] = (float) (wn.amplitude * q->amplitude * fastPerlin(q, 2 * i * STEP / 4 * f * q->lacunarity, 0, 2 * j * STEP / 4 * f * q->lacunarity)); });
        // the mean over size x size even steps from the even step (2i, 2j)
        std::vector<double> rows((size_t) n * n); wide[half].resize((size_t) n * n);
        for (int i = 0; i < n; i++) { const float *in = &w[(size_t) i * n]; double acc = 0; for (int d = 0; d < size; d++) acc += in[d % n]; for (int j = 0; j < n; j++) { rows[(size_t) i * n + j] = acc; acc += in[(j + size) % n] - in[j]; } }
        for (int j = 0; j < n; j++) { double acc = 0; for (int d = 0; d < size; d++) acc += rows[(size_t) (d % n) * n + j]; for (int i = 0; i < n; i++) { wide[half][(size_t) i * n + j] = (float) (acc / (size * size)); acc += rows[(size_t) ((i + size) % n) * n + j] - rows[(size_t) i * n + j]; } }
    }
    double tablesDone = seconds();

    // 2. the mean cheese of every window (periodic), per stack; a pair is the window origins (A index, B index)
    struct Pair { int a, b; float meanCheese; int16_t counts[LEVELS]; int open; bool sample; long gi, gj; float classBox[2][2], need; };   // classBox: the box weirdness of its four classes of places; need: what a place of it has to have // sample: one of the sixteenth of the pairs with the lowest sums (see the terrain step)
    std::vector<Pair> pairs, parts[2]; float lowestMean = 0, usedThreshold = 0;
    auto selectPairs = [&](int select, std::vector<Pair> &pairs, size_t wantPairs) {
    std::vector<float> mean[2];
    auto means = [&](int half) {
        const Tables &t = table[half]; int n = t.n;
        std::vector<double> column((size_t) n * n);
        for (size_t i = 0; i < column.size(); i++) column[i] = t.sum[i];
        std::vector<double> rows((size_t) n * n);
        for (int i = 0; i < n; i++) { // running sums along j, then along i
            const double *in = &column[(size_t) i * n]; double s = 0;
            for (int d = 0; d < select; d++) s += in[d];
            for (int j = 0; j < n; j++) { rows[(size_t) i * n + j] = s; s += in[(j + select) % n] - in[j]; }
        }
        mean[half].resize((size_t) n * n);
        std::vector<double> running(n, 0.0);
        for (int d = 0; d < select; d++) for (int j = 0; j < n; j++) running[j] += rows[(size_t) d * n + j];
        for (int i = 0; i < n; i++) {
            const double *out_ = &rows[(size_t) i * n], *in_ = &rows[(size_t) ((i + select) % n) * n];
            for (int j = 0; j < n; j++) { mean[half][(size_t) i * n + j] = (float) (running[j] / (select * select * MEAN_LEVELS)); running[j] += in_[j] - out_[j]; }
        }
    };
    { std::thread other(means, 1); means(0); other.join(); }
    if (direct) {
        // The ring's windows one by one. c = the window centre's world index (centre at c x STEP); the select x select
        // window's origin is c - select / 2, the 16 x 16 window's with the same centre c - WINDOW / 2. Two passes over the
        // ring: the counts of the sums by bucket (which give the threshold for wantPairs windows), then the windows under it.
        float lowA = mean[0][0], lowB = mean[1][0];
        for (float v : mean[0]) lowA = std::min(lowA, v);
        for (float v : mean[1]) lowB = std::min(lowB, v);
        const float lowest = lowA + lowB, perUnit = 8192.0f; const int BUCKETS = 4 * 8192;
        const double limit = std::min(ringOut, BORDER * 1.5); const long reach = (long) std::ceil(limit / STEP) + 1, edge = (long) std::floor((BORDER - WINDOW * STEP) / STEP);
        auto wrap = [](long v, int n) { return (int) (((v % n) + n) % n); };
        auto rows = [&](int part, int parts, std::vector<uint64_t> *histogram, int limitBucket, std::vector<Pair> *out) {
            for (long c = -reach + part; c <= reach; c += parts) {
                const double x = c * STEP; if (std::fabs(x) >= limit) continue;
                const long g = c - select / 2, big = c - WINDOW / 2;
                if (big < -edge || big > edge) continue; // (the world border)
                const double zOut = std::sqrt(std::max(0.0, limit * limit - x * x)), zIn = std::fabs(x) < ringIn ? std::sqrt(ringIn * ringIn - x * x) : 0.0;
                const float *rowA = &mean[0][(size_t) wrap(g, NA) * NA], *rowB = &mean[1][(size_t) wrap(g, NB) * NB];
                const long last = (long) std::floor(zOut / STEP), first = zIn > 0 ? (long) std::ceil(zIn / STEP) : 0;
                for (int side = 0; side < 2; side++) {
                    long from = side ? first : -last, to = side ? last : -first; if (first == 0 && side == 0) to = -1; // (centre 0 goes with the second half)
                    from = std::max(from, -edge + WINDOW / 2); to = std::min(to, edge + WINDOW / 2);
                    if (from > to) continue;
                    int ja = wrap(from - select / 2, NA), jb = wrap(from - select / 2, NB);
                    for (long cz = from; cz <= to; cz++) {
                        const float sum = rowA[ja] + rowB[jb]; const int bucket = std::min(BUCKETS - 1, (int) ((sum - lowest) * perUnit));
                        if (histogram) (*histogram)[bucket]++;
                        else if (bucket <= limitBucket) {
                            const double z = cz * STEP, d = std::sqrt(x * x + z * z);
                            if (d >= ringIn && d < ringOut) { Pair p; p.gi = big; p.gj = cz - WINDOW / 2; p.a = wrap(p.gi, NA) * NA + wrap(p.gj, NA); p.b = wrap(p.gi, NB) * NB + wrap(p.gj, NB); p.meanCheese = sum; p.sample = true; out->push_back(p); }
                        }
                        if (++ja == NA) ja = 0; if (++jb == NB) jb = 0;
                    }
                }
            }
        };
        const int parts = std::max(1, threads / 2); // (two window sizes are selected at the same time)
        std::vector<std::vector<uint64_t>> histograms(parts, std::vector<uint64_t>(BUCKETS, 0));
        { std::vector<std::thread> pool; for (int t = 0; t < parts; t++) pool.emplace_back(rows, t, parts, &histograms[t], 0, (std::vector<Pair> *) NULL); for (auto &t : pool) t.join(); }
        uint64_t total = 0, running = 0; int limitBucket = BUCKETS - 1;
        for (int b = 0; b < BUCKETS; b++) for (int t = 0; t < parts; t++) total += histograms[t][b];
        for (int b = 0; b < BUCKETS; b++) { for (int t = 0; t < parts; t++) running += histograms[t][b]; if (running >= wantPairs) { limitBucket = b; break; } }
        std::vector<std::vector<Pair>> found(parts);
        { std::vector<std::thread> pool; for (int t = 0; t < parts; t++) pool.emplace_back(rows, t, parts, (std::vector<uint64_t> *) NULL, limitBucket, &found[t]); for (auto &t : pool) t.join(); }
        pairs.clear(); for (auto &f : found) pairs.insert(pairs.end(), f.begin(), f.end());
        fprintf(stderr, "ring %.0f..%.0f, %d x %d windows: %llu positions, %zu taken (mean cheese <= %.4f; lowest possible %.4f)\n", ringIn, ringOut, select, select, (unsigned long long) total, pairs.size(), lowest + (limitBucket + 1) / perUnit, lowest);
        if (select == 8) { lowestMean = lowest; usedThreshold = lowest + (limitBucket + 1) / perUnit; }
        return;
    }
    // The pairs with the lowest sums, without sorting anything: the window means go into buckets of 1/8192 above each
    // stack's minimum (the B windows by their remainders mod 3, as only windows with the same remainders meet in the
    // world). The count of pairs whose buckets add up to at most T is a sum over A buckets of B counts, so the
    // threshold that gives wantPairs pairs is a search over T; then every A window takes the B windows of its class
    // from the buckets under its limit.
    const int BUCKETS = 8192; const float perUnit = 8192.0f;
    float lowA = mean[0][0], lowB = mean[1][0];
    for (float v : mean[0]) lowA = std::min(lowA, v);
    for (float v : mean[1]) lowB = std::min(lowB, v);
    auto bucketOf = [&](float v, float low) { return std::min(BUCKETS - 1, (int) ((v - low) * perUnit)); };
    auto classOfA = [&](int a) { return ((a / NA) % 3) * 3 + (a % NA) % 3; };
    std::vector<uint32_t> countA(9 * BUCKETS, 0), startB(9 * (BUCKETS + 1), 0); std::vector<int> orderB((size_t) NB * NB);
    for (int a = 0; a < NA * NA; a++) countA[classOfA(a) * BUCKETS + bucketOf(mean[0][a], lowA)]++;
    for (int i = 0; i < NB; i++) for (int j = 0; j < NB; j++) startB[((i % 3) * 3 + j % 3) * (BUCKETS + 1) + bucketOf(mean[1][i * NB + j], lowB) + 1]++;
    std::vector<uint32_t> classStart(10, 0);
    for (int c = 0; c < 9; c++) { // startB[c][k] = B windows of class c in the buckets under k (and where they start in orderB)
        uint32_t *s = &startB[c * (BUCKETS + 1)];
        for (int k = 0; k < BUCKETS; k++) s[k + 1] += s[k];
        classStart[c + 1] = classStart[c] + s[BUCKETS];
    }
    { std::vector<uint32_t> next(9 * BUCKETS);
      for (int c = 0; c < 9; c++) for (int k = 0; k < BUCKETS; k++) next[c * BUCKETS + k] = classStart[c] + startB[c * (BUCKETS + 1) + k];
      for (int i = 0; i < NB; i++) for (int j = 0; j < NB; j++) { int b = i * NB + j; orderB[next[((i % 3) * 3 + j % 3) * BUCKETS + bucketOf(mean[1][b], lowB)]++] = b; } }
    auto pairsUpTo = [&](int total) { // pairs whose buckets add up to at most total
        uint64_t pairs = 0;
        for (int c = 0; c < 9; c++) for (int a = 0; a <= total && a < BUCKETS; a++) if (countA[c * BUCKETS + a]) pairs += (uint64_t) countA[c * BUCKETS + a] * startB[c * (BUCKETS + 1) + std::min(total - a, BUCKETS - 1) + 1];
        return pairs;
    };
    int lo = 0, hi = 2 * BUCKETS - 2;
    while (lo < hi) { int mid = (lo + hi) / 2; if (pairsUpTo(mid) >= wantPairs) hi = mid; else lo = mid + 1; }
    const float lowest = lowA + lowB, threshold = lowest + (lo + 1) / perUnit; const int shift = (WINDOW - select) / 2; // (the origin of the 16 x 16 window with the same centre)
    pairs.clear();
    // With WBOX_MIN the box weirdness is tested here, pair by pair, and from memory that is read in order: a window's
    // part of it depends on the window and on which of the wide octave's two periods it is in (per axis: 4 values), so
    // the B windows get theirs once, in the order the loop below takes them, and an A window its own when its turn comes.
    // A pair's places are COMMON apart, an odd count of both periods: 2 x 2 classes of places, each one sum of an A and a B part.
    const int ownShift = (WINDOW - ownSize) / 2;
    auto wideIndex = [&](int origin, int period, int n) { const int si = (origin + n * period + ownShift) % (2 * n); return ((si + (si & 1)) / 2) % n; };   // the wide table's index of the first even step of the box: the window's origin is at origin + n x period (mod 2n)
    auto boxParts = [&](int half, int origin16, float *out) { const int n = half ? NB : NA, i = origin16 / n, j = origin16 % n;
        for (int pi = 0; pi < 2; pi++) for (int pj = 0; pj < 2; pj++) out[pi * 2 + pj] = box[half][origin16] + wide[half][(size_t) wideIndex(i, pi, n) * n + wideIndex(j, pj, n)]; };
    auto origin16 = [&](int w, int n) { return ((w / n - shift + n) % n) * n + (w % n - shift + n) % n; };
    // (per B window, in the loop's order: its mean cheese, the largest of its four parts, the four parts)
    std::unique_ptr<float[]> partB; int stepOf[331];
    if (haveWide) {
        partB.reset(new float[6 * (size_t) NB * NB]);   // (only the ranges that are read get made)
        uint32_t most[9] = {0};
        for (int a = 0; a < NA * NA; a++) { const int room = lo - bucketOf(mean[0][a], lowA); if (room < 0) continue; const int c = classOfA(a); most[c] = std::max(most[c], startB[c * (BUCKETS + 1) + std::min(room, BUCKETS - 1) + 1]); }
        for (int c = 0; c < 9; c++) for (uint32_t k = classStart[c]; k < classStart[c] + most[c]; k++) { float *q = &partB[6 * (size_t) k]; boxParts(1, origin16(orderB[k], NB), q + 2); q[0] = mean[1][orderB[k]]; q[1] = std::max(std::max(q[2], q[3]), std::max(q[4], q[5])); }
        for (int d = 0; d < 331; d++) stepOf[d] = (d * 276) % 331;
    }
    size_t candidates = 0;
    for (int a = 0; a < NA * NA; a++) {
        int room = lo - bucketOf(mean[0][a], lowA); // the B buckets this window may take
        if (room < 0) continue;
        int c = classOfA(a); const uint32_t *s = &startB[c * (BUCKETS + 1)];
        const int a16 = origin16(a, NA), ia = a16 / NA, ja = a16 % NA; float partA[4] = {0, 0, 0, 0};
        if (haveWide) boxParts(0, a16, partA);
        const float mostA = std::max(std::max(partA[0], partA[1]), std::max(partA[2], partA[3])), meanA = mean[0][a];
        const uint32_t end = classStart[c] + s[std::min(room, BUCKETS - 1) + 1]; candidates += end - classStart[c];
        for (uint32_t k = classStart[c]; k < end; k++) {
            // WBOX_SLOPE: a pair with less cave noise than the last one taken needs that much less weirdness per unit of mean cheese
            float need = wboxMin;
            if (haveWide) { const float *q = &partB[6 * (size_t) k]; need = wboxMin - wboxSlope * std::max(0.0f, threshold - (meanA + q[0])); if (mostA + q[1] < need) continue; }
            int b = orderB[k];
            Pair p; p.gi = p.gj = 0; p.a = a16; p.b = origin16(b, NB); p.meanCheese = mean[0][a] + mean[1][b]; p.need = need;
            if (haveWide) {
                // the place g = ia + 1011 k (k as in the terrain step); it is in period k & 1 of stack A and ((g - ib) / 993) & 1 of stack B
                const int ib = p.b / NB, jb = p.b % NB, ki = stepOf[(((ib - ia) / 3) % 331 + 331) % 331], kj = stepOf[(((jb - ja) / 3) % 331 + 331) % 331];
                const int ai = ki & 1, aj = kj & 1, bi = ((ia + 1011 * ki - ib) / 993) & 1, bj = ((ja + 1011 * kj - jb) / 993) & 1; const float *pb = &partB[6 * (size_t) k + 2]; bool any = false;
                for (int m = 0; m < 2; m++) for (int q = 0; q < 2; q++) { const float v = partA[((ai ^ m) << 1) | (aj ^ q)] + pb[((bi ^ m) << 1) | (bj ^ q)]; p.classBox[m][q] = v; any |= v >= need; }
                if (!any) continue;
            }
            pairs.push_back(p);
        }
    }
    if (haveWide) fprintf(stderr, "windows of %d: %zu of %zu pairs have a place with a box weirdness of %.2f or more\n", select, pairs.size(), candidates, wboxMin);
    { // the sample: the pairs under the threshold that gives a sixteenth as many
        int lo16 = 0, hi16 = lo;
        while (lo16 < hi16) { int mid = (lo16 + hi16) / 2; if (pairsUpTo(mid) >= wantPairs / 16) hi16 = mid; else lo16 = mid + 1; }
        const float sampleThreshold = lowest + (lo16 + 1) / perUnit;
        for (Pair &p : pairs) p.sample = p.meanCheese <= sampleThreshold;
    }
    if (&pairs == &parts[0]) { lowestMean = lowest; usedThreshold = threshold; }
    };
    if (selectB > 0) { std::thread other(selectPairs, selectB, std::ref(parts[1]), wantPairs); selectPairs(selectA, parts[0], wantPairs); other.join(); }
    else selectPairs(selectA, parts[0], 2 * wantPairs);
    const size_t firstPart = parts[0].size();
    pairs = std::move(parts[0]); pairs.insert(pairs.end(), parts[1].begin(), parts[1].end()); parts[1].clear(); parts[1].shrink_to_fit();
    std::vector<float> meanRank;   // (oracle) a pair's rank by mean cheese within its window size, as a fraction
    if (oracle) { meanRank.resize(pairs.size()); for (int part = 0; part < 2; part++) { const size_t from = part ? firstPart : 0, to = part ? pairs.size() : firstPart; std::vector<int> order; for (size_t i = from; i < to; i++) order.push_back((int) i);
        std::sort(order.begin(), order.end(), [&](int x, int y) { return pairs[x].meanCheese < pairs[y].meanCheese; }); for (size_t r = 0; r < order.size(); r++) meanRank[order[r]] = (float) r / order.size(); } }
    double pairsDone = seconds();
    if (getenv("STATS") && atoi(getenv("STATS")) == 2) { printf("%lld %.4f %.4f\n", (long long) seed, lowestMean, usedThreshold); return 0; }   // the cave noise alone: the lowest window mean and the mean of the last pair taken

    // 3. the formula from the tables, per height (all 16 heights of a column at once, in 16-bit fixed point); first for
    //    the sample pairs only: those decide whether the seed is worth the rest (see the terrain step)
    const bool stats = getenv("STATS") != NULL;
    size_t completed = 0;
    auto complete = [&](bool sample) { // the rest of the noises for the columns of the windows of these pairs
        for (int half = 0; half < 2; half++) {
            Tables &t = table[half]; int n = t.n; std::vector<uint8_t> origin((size_t) n * n, 0), need((size_t) n * n, 0);
            for (size_t index = 0; index < pairs.size(); index++) if (stats || pairs[index].sample == sample) origin[half ? pairs[index].b : pairs[index].a] = 1;
            for (int i = 0; i < n; i++) for (int j = 0; j < n; j++) if (origin[(size_t) i * n + j]) {
                for (int di = 0; di < WINDOW; di++) { uint8_t *row = &need[(size_t) ((i + di) % n) * n]; for (int dj = 0; dj < WINDOW; dj++) row[(j + dj) % n] = 1; }
            }
            std::vector<int> list;
            for (int c = 0; c < n * n; c++) if (need[c] && !t.complete[c]) list.push_back(c);
            completed += list.size();
            parallel(threads, (int) list.size(), [&](int s) {
                int i = list[s] / n, j = list[s] % n; double x = i * STEP, z = j * STEP; Packed &p = t.columns[list[s]]; float c[LEVELS] = {0}, l[LEVELS] = {0}, f[LEVELS] = {0};
                addColumn(cheese, half, COARSE_OCTAVES, x, z, 0.6666666666666666, levelY(0), 16, MADE_LEVELS, c, BASE_OCTAVES); addColumn(layer, half, 99, x, z, 8.0, levelY(0), 16, MADE_LEVELS, l); addColumn(flood, half, 99, x, z, 0.67, levelY(0), 16, MADE_LEVELS, f);
                for (int k = 0; k < MADE_LEVELS; k++) {
                    p.cheese[k] = (int16_t) std::max(-32000L, std::min(32000L, p.cheese[k] + std::lrint(std::max(-3.9f, std::min(3.9f, c[k])) * 4096)));
                    p.layer[k] = (int8_t) std::lrint(std::max(-1.98f, std::min(1.98f, l[k])) * 64); p.flood[k] = (int8_t) std::lrint(std::max(-1.98f, std::min(1.98f, f[k])) * 64);
                }
                t.complete[list[s]] = 1;
            });
        }
    };
    auto formula = [&](bool sample) { complete(sample); parallel(threads, (int) pairs.size(), [&](int index) {
        if (!stats && pairs[index].sample != sample) return;
        Pair &p = pairs[index]; int ia = p.a / NA, ja = p.a % NA, ib = p.b / NB, jb = p.b % NB;
        const __m256i shift = _mm256_set1_epi16(1106), least = _mm256_set1_epi16(-4096), most = _mm256_set1_epi16(90), dry = _mm256_set1_epi16(26), zero = _mm256_setzero_si256();
        __m256i count = zero;
        for (int di = 0; di < WINDOW; di++) {
            const Packed *rowA = &table[0].columns[(size_t) ((ia + di) % NA) * NA], *rowB = &table[1].columns[(size_t) ((ib + di) % NB) * NB];
            for (int dj = 0; dj < WINDOW; dj++) {
                const Packed &a = rowA[(ja + dj) % NA], &b = rowB[(jb + dj) % NB];
                // open: clamp(cheese + 0.27, -1, ..) + 4 layer^2 < 0, and floodedness <= 0.4
                __m256i c = _mm256_max_epi16(_mm256_adds_epi16(_mm256_adds_epi16(_mm256_load_si256((const __m256i *) a.cheese), _mm256_load_si256((const __m256i *) b.cheese)), shift), least);
                __m256i l = _mm256_add_epi16(_mm256_cvtepi8_epi16(_mm_load_si128((const __m128i *) a.layer)), _mm256_cvtepi8_epi16(_mm_load_si128((const __m128i *) b.layer)));
                l = _mm256_min_epi16(_mm256_abs_epi16(l), most); // 4 layer^2 in 1/4096 is 4 l^2 with l in 1/64; past 90 it is far over 1 anyway
                __m256i value = _mm256_adds_epi16(c, _mm256_slli_epi16(_mm256_mullo_epi16(l, l), 2));
                __m256i wet = _mm256_add_epi16(_mm256_cvtepi8_epi16(_mm_load_si128((const __m128i *) a.flood)), _mm256_cvtepi8_epi16(_mm_load_si128((const __m128i *) b.flood)));
                count = _mm256_sub_epi16(count, _mm256_and_si256(_mm256_cmpgt_epi16(zero, value), _mm256_cmpgt_epi16(dry, wet)));
            }
        }
        alignas(32) int16_t counts[16]; _mm256_store_si256((__m256i *) counts, count);
        p.open = 0;
        for (int k = 0; k < LEVELS; k++) { p.counts[k] = counts[k]; p.open += counts[k]; }
    }); };
    if (stats) { for (Pair &p : pairs) p.sample = true; formula(true); }
    double formulaDone = seconds();
    if (stats) {
        std::vector<int> byOpen(pairs.size());
        for (size_t i = 0; i < byOpen.size(); i++) byOpen[i] = (int) i;
        std::sort(byOpen.begin(), byOpen.end(), [&](int x, int y) { return pairs[x].open > pairs[y].open; }); // what the tables alone say about the seed: "seed lowestMean threshold", the open counts of the pairs ranked 1, 100, 1000, 10000, 100000, and the same for the open points of the 7 lowest heights (y -40 .. 56)
        std::vector<int> low(pairs.size());
        for (size_t i = 0; i < pairs.size(); i++) { int sum = 0; for (int k = 0; k < 7; k++) sum += pairs[i].counts[k]; low[i] = sum; }
        std::sort(low.begin(), low.end(), std::greater<int>());
        printf("%lld %.4f %.4f", (long long) seed, lowestMean, usedThreshold);
        for (size_t rank : {0, 99, 999, 9999, 99999}) printf(" %d", pairs[byOpen[std::min(pairs.size() - 1, rank)]].open);
        for (size_t rank : {0, 99, 999, 9999, 99999}) printf(" %d", low[std::min(low.size() - 1, rank)]);
        printf("\n");
        return 0;
    }

    // 4. the terrain at every place of every pair
    // OWN_KEEP=share: of the pairs picked by cave noise, keep that share with the highest "own" weirdness. The weirdness'
    // octaves -6 and -5 repeat with the cave noise (65,536 blocks for stack A), so their mean over a pair's window is the
    // same at every place of the pair: a pair whose own weirdness is low has little chance of a lobe anywhere.
    size_t firstKept = firstPart;
    if (ownPrune) {
        const double keep = atof(getenv("OWN_KEEP"));
        std::vector<float> value(pairs.size());
        if (!ownMax) for (size_t i = 0; i < pairs.size(); i++) value[i] = box[0][pairs[i].a] + box[1][pairs[i].b];
        else parallel(threads, (int) pairs.size(), [&](int i) {   // OWN_MAX=1: the best such box anywhere inside the 16 x 16 window (a lobe need not be in the middle)
            const Pair &p = pairs[i]; const int reach = (WINDOW - ownSize) / 2; float best = -1e9f;
            for (int di = -reach; di <= reach; di += 2) for (int dj = -reach; dj <= reach; dj += 2) {
                float v = 0;
                for (int half = 0; half < 2; half++) { const int n = table[half].n, origin = half ? p.b : p.a; v += box[half][(size_t) (((origin / n + di) % n + n) % n) * n + ((origin % n + dj) % n + n) % n]; }
                best = std::max(best, v);
            }
            value[i] = best; });
        std::vector<float> sorted(value); const size_t cut = (size_t) ((1 - keep) * sorted.size()); std::nth_element(sorted.begin(), sorted.begin() + cut, sorted.end()); const float least = sorted[cut];
        size_t out = 0, first = 0;
        for (size_t i = 0; i < pairs.size(); i++) if (value[i] >= least) { if (i < firstPart) first++; pairs[out++] = pairs[i]; }
        fprintf(stderr, "own weirdness: %zu of %zu pairs kept (from %.3f)\n", out, pairs.size(), least); pairs.resize(out); firstKept = first;
    }
    const DoublePerlinNoise &tNoise = biomeNoise.climate[NP_TEMPERATURE]; const PerlinNoise *tA0 = tNoise.octA.octaves, *tB0 = tNoise.octB.octaves; const double tAmp = tNoise.amplitude, tF = 337.0 / 331.0;
    const double slopeSlack = getenv("SLOPE_SLACK") ? atof(getenv("SLOPE_SLACK")) : 0.04, narrowMost = getenv("NARROW_MOST") ? atof(getenv("NARROW_MOST")) : 0.17;
    if (tNoise.octA.octcnt != 2 || tNoise.octB.octcnt != 2) { fprintf(stderr, "the temperature noise is not two octaves a stack\n"); return 1; }
    struct Place { int pair; long gi, gj; float surface; int pre; int quarters; int preRank; uint16_t mask; float floor; int coarsePure, coarseIce, coarseTotal, coarseSealed; float tC, cC, w8, w4, index, wbox, own; };   // mask: the quarters in the ice caves box; floor: the mean sea floor over them; coarse*: the first look's clusters
    std::vector<std::vector<Place>> viable(threads); std::atomic<size_t> allPlaces(0), samples(0), afterWide(0); std::vector<std::array<size_t, 40>> wideSeen(threads, std::array<size_t, 40>{});
    const bool wideStats = getenv("WBOX") != NULL; const size_t wideSample = getenv("WBOX_SAMPLE") ? strtoull(getenv("WBOX_SAMPLE"), 0, 10) : 0; const bool wideCheck = getenv("WBOX_CHECK") != NULL; std::atomic<int> checksLeft(wideCheck ? 400 : 0);
    // Every place of every pair, by its climate alone: no table is needed for that, and only one pair in thirty has a
    // place under ice caves. The formula (the rest of the noise columns and the open points per height) is then made
    // for those pairs only: it was the most expensive step when every pair got it.
    int shownCut = 0, cut = 0;
    {
        std::atomic<size_t> nextPair(0); std::vector<std::thread> pool;
        for (int t = 0; t < threads; t++) pool.emplace_back([&, t] {
            size_t count = 0, taken = 0, passed = 0;
            for (size_t index; (index = nextPair++) < pairs.size();) {
                const Pair &p = pairs[index]; int ia = p.a / NA, ja = p.a % NA, ib = p.b / NB, jb = p.b % NB;
                // g = ia + 1011 k with 18 k = ib - ia (mod 993): k = ((ib - ia) / 3) * 276 (mod 331), 276 being 1 / 6 mod 331
                auto solve = [](int a, int b) { int d = (((b - a) / 3) % 331 + 331) % 331; return (long) a + 1011L * ((d * 276) % 331); };
                long gi0 = solve(ia, ib), gj0 = solve(ja, jb);
                // The box weirdness of the pair's places: places 2 COMMON apart share the wide octave's tables too (COMMON is
                // an odd count of both periods), so a pair has 2 x 2 classes of places, each with one value: 8 lookups a pair.
                float classBox[2][2] = {{0, 0}, {0, 0}}, ownPart = 0;
                if (haveWide && !direct) { for (int m = 0; m < 2; m++) for (int k = 0; k < 2; k++) classBox[m][k] = p.classBox[m][k]; if (wideSample) ownPart = box[0][p.a] + box[1][p.b]; }
                else if (haveWide) {
                    auto mod = [](long v, int n) { return (size_t) (((v % n) + n) % n); };
                    size_t rowA[2], rowB[2], colA[2], colB[2];
                    for (int m = 0; m < 2; m++) {   // (the even steps of the middle ownSize steps: from the first even step at or after g + shift)
                        const long si = (direct ? p.gi : gi0) + m * (long) COMMON + (WINDOW - ownSize) / 2, sj = (direct ? p.gj : gj0) + m * (long) COMMON + (WINDOW - ownSize) / 2, ei = (si + (si & 1)) / 2, ej = (sj + (sj & 1)) / 2;
                        rowA[m] = mod(ei, NA) * NA; rowB[m] = mod(ei, NB) * NB; colA[m] = mod(ej, NA); colB[m] = mod(ej, NB);
                    }
                    ownPart = box[0][p.a] + box[1][p.b]; bool any = false;
                    for (int m = 0; m < 2; m++) for (int k = 0; k < 2; k++) { classBox[m][k] = ownPart + wide[0][rowA[m] + colA[k]] + wide[1][rowB[m] + colB[k]]; any |= classBox[m][k] >= wboxMin; }
                    if (!any && !wideStats) continue;
                }
                for (long gi = direct ? p.gi : gi0 - 2L * COMMON; gi <= (direct ? p.gi : gi0 + COMMON); gi += COMMON) for (long gj = direct ? p.gj : gj0 - 2L * COMMON; gj <= (direct ? p.gj : gj0 + COMMON); gj += COMMON) {
                    if (ringed && !inRing(gi, gj)) continue;
                    double x = gi * STEP, z = gj * STEP, side = WINDOW * STEP;
                    if (x < -BORDER || z < -BORDER || x + side > BORDER || z + side > BORDER) continue;
                    count++;
                    float wbox = 0;
                    if (haveWide) {
                        wbox = direct ? classBox[0][0] : classBox[((gi - gi0) / COMMON) & 1][((gj - gj0) / COMMON) & 1];
                        if (wideSample && count % wideSample == 0) fprintf(stderr, "wsample %.4f %.4f %.4f\n", ownPart, wbox - ownPart, p.meanCheese);
                        if (wideStats) wideSeen[t][std::min(39, std::max(0, (int) floor((wbox + 1.0f) * 20)))]++;
                        if (wideCheck && checksLeft-- > 0) { double direct = 0; const int sh = (WINDOW - ownSize) / 2; for (int di = 0; di < ownSize; di++) for (int dj = 0; dj < ownSize; dj++) direct += firstOctaves(&wn, (gi + sh + di) * STEP / 4, (gj + sh + dj) * STEP / 4, 99); fprintf(stderr, "wbox check: table %.4f direct %.4f\n", wbox, direct / (ownSize * ownSize)); }
                        if (wbox < (direct ? wboxMin : p.need)) continue;
                        passed++;
                    }
                    // Ice caves there? The temperature first. Its wide octave (4,096 blocks) is nearly a plane across a window:
                    // the value and slope in the middle (two noise samples) give it at the 16 quarter centres within 0.05
                    // (99.9%), and the narrow octave adds at most 0.2 (99.9%: 0.18). So most windows go after two samples, and
                    // the rest ask only the quarters that can be at -0.8. A cold quarter then needs the weirdness (one in eight
                    // is over 0.5), the continentalness, the exact temperature and the humidity, in that order.
                    const double mx = floor((x + side / 2) / 4), mz = floor((z + side / 2) / 4); double a1, a3, b1, b3;
                    const double va = fastPerlinSlope(tA0, mx * tA0->lacunarity, mz * tA0->lacunarity, &a1, &a3), vb = fastPerlinSlope(tB0, mx * tF * tB0->lacunarity, mz * tF * tB0->lacunarity, &b1, &b3);
                    const double wide = tAmp * (tA0->amplitude * va + tB0->amplitude * vb) - slopeSlack, sx = tAmp * (tA0->amplitude * a1 * tA0->lacunarity + tB0->amplitude * b1 * tF * tB0->lacunarity),
                                 sz = tAmp * (tA0->amplitude * a3 * tA0->lacunarity + tB0->amplitude * b3 * tF * tB0->lacunarity);
                    if (wide - (fabs(sx) + fabs(sz)) * (3 * side / 8 / 4) > -0.8 + narrowMost) continue;
                    int inBox = 0; unsigned mask = 0; double floorSum = 0;
                    for (int k = 0; k < 16; k++) {
                        const int qx = (int) floor((x + side * (2 * (k / 4) + 1) / 8) / 4), qz = (int) floor((z + side * (2 * (k % 4) + 1) / 8) / 4);
                        const double guess = wide + sx * (qx - mx) + sz * (qz - mz);
                        if (guess > -0.8 + narrowMost) continue;
                        taken++;
                        if (guess + tAmp * (tA0[1].amplitude * fastPerlin(tA0 + 1, qx * tA0[1].lacunarity, 0, qz * tA0[1].lacunarity) + tB0[1].amplitude * fastPerlin(tB0 + 1, qx * tF * tB0[1].lacunarity, 0, qz * tF * tB0[1].lacunarity)) > -0.8) continue;
                        auto fixed = [](float v) { return (long) (10000.0F * v); };
                        IceClimate c; c.w = (float) firstOctaves(&biomeNoise.climate[NP_WEIRDNESS], qx, qz, 99); if (fixed(c.w) < 5000 || fixed(c.w) > 11000) continue;
                        c.c = (float) firstOctaves(&biomeNoise.climate[NP_CONTINENTALNESS], qx, qz, 99); if (fixed(c.c) < -10500 || fixed(c.c) > -1900) continue;
                        c.t = (float) firstOctaves(&biomeNoise.climate[NP_TEMPERATURE], qx, qz, 99); if (fixed(c.t) > -8000) continue;
                        c.h = (float) firstOctaves(&biomeNoise.climate[NP_HUMIDITY], qx, qz, 99); if (fixed(c.h) > 7000) continue;
                        c.e = (float) firstOctaves(&biomeNoise.climate[NP_EROSION], qx, qz, 99);
                        if (iceBoxDistance(&c) != 0) continue;
                        float par[] = {c.c, c.e, -3.0F * (fabsf(fabsf(c.w) - 0.6666667F) - 0.33333334F), c.w};
                        inBox++; mask |= 1u << k; floorSum += 128.0 * (1.0 - 83.0 / 160.0 + getSpline(biomeNoise.sp, par) + 0.015F);
                    }
                    if (inBox < minQuarters) continue;
                    Place q; q.pair = (int) index; q.gi = gi; q.gj = gj; q.surface = 88; q.pre = 0; q.quarters = inBox; q.preRank = 0; q.mask = (uint16_t) mask; q.floor = (float) (floorSum / std::max(1, inBox)); q.coarsePure = q.coarseIce = q.coarseTotal = q.coarseSealed = 0; q.tC = q.cC = q.w8 = q.w4 = 0; q.index = -1e30f; q.wbox = wbox; q.own = ownPart;   // (surface 88: the fine tables are made up to y 80)
                    viable[t].push_back(q);
                }
            }
            allPlaces += count; samples += taken; afterWide += passed;
        });
        for (auto &t : pool) t.join();
    }
    double climateDone = seconds();
    if (haveWide) { size_t sum[40] = {0}; for (auto &v : wideSeen) for (int k = 0; k < 40; k++) sum[k] += v[k];
        fprintf(stderr, "weirdness box: %zu places at %.2f or more", afterWide.load(), wboxMin); if (wideStats) { fprintf(stderr, " of %zu; places by box mean from -1 in steps of 0.05:", allPlaces.load()); for (int k = 0; k < 40; k++) fprintf(stderr, " %zu", sum[k]); } fprintf(stderr, "\n"); }
    for (Pair &p : pairs) p.sample = false;
    for (auto &v : viable) for (const Place &q : v) pairs[q.pair].sample = true;
    if (!stats) complete(true);   // (the rest of the noise columns under those pairs' windows)
    (void) shownCut; (void) cut; (void) minCut; (void) surfaceMin;
    std::vector<Place> places;
    for (auto &v : viable) places.insert(places.end(), v.begin(), v.end());
    size_t viablePlaces = places.size();
    static const char *FEATURES[] = {"look", "pure", "sealed", "ice", "total", "quarters", "floor", "t", "cold2", "c", "w8", "w4", "cheese8", "cheese12", "cheese16", "cheese20", "low8", "low12", "low16", "low20", "own8", "own12", "select", "mean", "wbox"};
    const int NF = sizeof FEATURES / sizeof FEATURES[0];
    // The index: a place's logit of holding a large cluster, from the coefficients in the file INDEX (lines "name value",
    // "intercept value"; names as in FEATURES). Without the file the first look's own score ranks the places.
    bool haveIndex = false; double coefficient[64] = {0}, intercept = 0;
    if (getenv("INDEX")) {
        FILE *f = fopen(getenv("INDEX"), "r"); char name[64]; double value;
        if (f) { while (fscanf(f, "%63s %lf", name, &value) == 2) { if (!strcmp(name, "intercept")) intercept = value; for (int k = 0; k < NF; k++) if (!strcmp(name, FEATURES[k])) coefficient[k] = value; } fclose(f); haveIndex = true; }
        else fprintf(stderr, "no index file %s\n", getenv("INDEX"));
    }
    bool pairTerms = false; for (int k = 12; k < 22; k++) pairTerms |= coefficient[k] != 0;
    // Features of a pair for the ranking index (placefit.py): the cave noise's mean over windows of 8, 12, 16 and 20 steps
    // about the middle (five heights, and the three lowest), and the pair's own weirdness: octaves -6 and -5 repeat with
    // the cave noise, so their mean over the middle 8 and 12 steps is the same at every place of the pair.
    const bool wantFeatures = oracle || pairTerms; const int boxSizes[4] = {8, 12, 16, 20}; std::vector<float> boxAll[2][4], boxLow[2][4], own8[2], own12[2];
    if (wantFeatures) {
        auto boxMean = [](const std::vector<float> &col, int n, int s) {
            std::vector<double> rows((size_t) n * n); std::vector<float> out((size_t) n * n);
            for (int i = 0; i < n; i++) { const float *in = &col[(size_t) i * n]; double acc = 0; for (int d = 0; d < s; d++) acc += in[d]; for (int j = 0; j < n; j++) { rows[(size_t) i * n + j] = acc; acc += in[(j + s) % n] - in[j]; } }
            std::vector<double> run(n, 0.0); for (int d = 0; d < s; d++) for (int j = 0; j < n; j++) run[j] += rows[(size_t) d * n + j];
            for (int i = 0; i < n; i++) { const double *o = &rows[(size_t) i * n], *in = &rows[(size_t) ((i + s) % n) * n]; for (int j = 0; j < n; j++) { out[(size_t) i * n + j] = (float) (run[j] / (s * s)); run[j] += in[j] - o[j]; } }
            return out; };
        const DoublePerlinNoise &wn = biomeNoise.climate[NP_WEIRDNESS];
        for (int half = 0; half < 2; half++) {
            const int n = table[half].n; std::vector<float> own((size_t) n * n); const OctaveNoise &o = half ? wn.octB : wn.octA; const double f = half ? 337.0 / 331.0 : 1.0;
            parallel(threads, n, [&](int i) { for (int j = 0; j < n; j++) { double v = 0; for (int k = 1; k < o.octcnt; k++) { const PerlinNoise *q = o.octaves + k; v += q->amplitude * fastPerlin(q, i * STEP / 4 * f * q->lacunarity, 0, j * STEP / 4 * f * q->lacunarity); } own[(size_t) i * n + j] = (float) (v * wn.amplitude); } });
            own8[half] = boxMean(own, n, 8); own12[half] = boxMean(own, n, 12);
            for (int k = 0; k < 4; k++) { boxAll[half][k] = boxMean(table[half].sum, n, boxSizes[k]); boxLow[half][k] = boxMean(table[half].low, n, boxSizes[k]); }
        }
    }
    auto about = [&](const std::vector<float> &partA, const std::vector<float> &partB, const Pair &p, int size) {   // the window of that many steps about the middle of the pair's 16 x 16 one: A part + B part
        const int shift = (WINDOW - size) / 2; float v = 0;
        for (int half = 0; half < 2; half++) { const int n = table[half].n, origin = half ? p.b : p.a, i = ((origin / n + shift) % n + n) % n, j = ((origin % n + shift) % n + n) % n; v += (half ? partB : partA)[(size_t) i * n + j]; }
        return v; };
    auto featuresOf = [&](const Place &q, float *x) {
        const Pair &p = pairs[q.pair]; const float cold = std::min(q.tC + 0.8f, 0.0f);
        x[0] = (float) std::max(2 * q.coarseSealed, q.coarsePure); x[1] = (float) q.coarsePure; x[2] = (float) q.coarseSealed; x[3] = (float) q.coarseIce; x[4] = (float) q.coarseTotal; x[5] = (float) q.quarters; x[6] = q.floor;
        x[7] = q.tC; x[8] = cold * cold; x[9] = q.cC; x[10] = q.w8; x[11] = q.w4;
        for (int k = 12; k < 22; k++) x[k] = 0;
        if (wantFeatures) { for (int k = 0; k < 4; k++) { x[12 + k] = about(boxAll[0][k], boxAll[1][k], p, boxSizes[k]) / MEAN_LEVELS; x[16 + k] = about(boxLow[0][k], boxLow[1][k], p, boxSizes[k]) / 3; }
            x[20] = about(own8[0], own8[1], p, 8); x[21] = about(own12[0], own12[1], p, 12); } x[22] = (size_t) q.pair < firstKept ? (float) selectA : (float) selectB; x[23] = p.meanCheese; x[24] = q.wbox; };
    const double lookStart = seconds();
    // 4b. A first look at every such place, on the coarse grid (16 x 16 columns of 64.8 blocks, the five heights y -40 .. 24):
    //     the clusters of points that are open and dry at that depth under the place's sea floor, with the place's quarters
    //     as its ice caves. coarsePure: the largest cluster inside them; coarseIce of coarseTotal: the cluster with the most
    //     points in them. The fine look (most of the scanner's time) then goes to the best of these.
    {
        const int CL = 5; const bool lookTables = haveWide && !getenv("LOOK_SAMPLED");   // the weirdness of the first look from the tables (all three octaves) instead of two sampled octaves
        const bool lookCheck = getenv("LOOK_CHECK") != NULL; std::vector<double> checkSum(threads, 0.0), checkMost(threads, 0.0); std::vector<size_t> checkCount(threads, 0);
        std::sort(places.begin(), places.end(), [](const Place &x, const Place &y) { return x.pair < y.pair; });
        std::vector<size_t> starts;
        for (size_t i = 0; i < places.size(); i++) if (i == 0 || places[i].pair != places[i - 1].pair) starts.push_back(i);
        starts.push_back(places.size());
        std::atomic<size_t> nextStart(0); std::vector<std::thread> lookers; std::vector<std::array<double, 4>> lookSpent(threads, std::array<double, 4>{0, 0, 0, 0});
        for (int t = 0; t < threads; t++) lookers.emplace_back([&, t] {
            auto tick = [] { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }; double mark = tick();
            auto lap = [&](int k) { const double now = tick(); lookSpent[t][k] += now - mark; mark = now; };
            uint8_t open[256], cell[256], seen[256]; int16_t wet[256][CL]; std::vector<std::pair<int, unsigned>> stack;
            auto fill = [](unsigned reached, unsigned all) { for (;;) { const unsigned next = (reached | (reached << 1) | (reached >> 1)) & all; if (next == reached) return reached; reached = next; } };
            for (size_t g; (g = nextStart++) + 1 < starts.size();) {
                const Pair &p = pairs[places[starts[g]].pair]; const int ia = p.a / NA, ja = p.a % NA, ib = p.b / NB, jb = p.b % NB;
                for (int di = 0; di < WINDOW; di++) {
                    const Packed *rowA = &table[0].columns[(size_t) ((ia + di) % NA) * NA], *rowB = &table[1].columns[(size_t) ((ib + di) % NB) * NB];
                    for (int dj = 0; dj < WINDOW; dj++) {
                        const Packed &a = rowA[(ja + dj) % NA], &b = rowB[(jb + dj) % NB]; unsigned bits = 0;
                        for (int k = 0; k < CL; k++) {
                            const int c = std::max(-4096, a.cheese[k] + b.cheese[k] + 1106), l = std::min(90, abs(a.layer[k] + b.layer[k]));
                            if (c + 4 * l * l < 0) bits |= 1u << k;
                            wet[di * WINDOW + dj][k] = (int16_t) (a.flood[k] + b.flood[k]);
                        }
                        open[di * WINDOW + dj] = (uint8_t) bits;
                    }
                }
                lap(0);
                for (size_t at = starts[g]; at < starts[g + 1]; at++) {
                    Place &q = places[at]; const double x = q.gi * STEP, z = q.gj * STEP, side = WINDOW * STEP;
                    // the place's climate, roughly: temperature, continentalness (its five widest octaves) and the sea floor at
                    // the 16 quarter centres, the weirdness on 8 x 8
                    float tq[16], cq[16], wq[64]; int most[16][CL]; unsigned allowed[16]; float depth0[16];
                    for (int k = 0; k < 16; k++) {
                        const double qx = floor((x + side * (2 * (k / 4) + 1) / 8) / 4), qz = floor((z + side * (2 * (k % 4) + 1) / 8) / 4);
                        cq[k] = (float) firstOctaves(&biomeNoise.climate[NP_CONTINENTALNESS], qx, qz, 5);
                        float par[] = {cq[k], 0.0f, 0.0f, 0.0f};   // (under the sea the floor depends on the continentalness alone; land is taken flat)
                        const double off = getSpline(biomeNoise.sp, par) + 0.015F, here = 128.0 * (1.0 - 83.0 / 160.0 + off); const bool sea = here < 63;
                        depth0[k] = (float) (1.0 - 83.0 / 160.0 + off); allowed[k] = 0;
                        for (int l = 0; l < CL; l++) {   // Aquifer.computeSurfaceLevel under a sea floor, as in the fine look
                            const int y = levelY(l); if (y <= here - (sea ? 8 : 24)) allowed[k] |= 1u << l;
                            most[k][l] = sea ? (int) std::floor(64 * (0.4 - 1.2 * std::min(1.0, std::max(0.0, 1.0 - (here + 8 - y) / 64.0)))) : 25;
                        }
                    }
                    // the air points of every column; the weirdness (its two wide octaves, on 8 x 8) only around columns that have some
                    uint8_t iceBits[256]; int points = 0; uint64_t wanted = 0;
                    for (int di = 0; di < WINDOW; di++) for (int dj = 0; dj < WINDOW; dj++) {
                        const int i = di * WINDOW + dj, quarter = (di / 4) * 4 + dj / 4;
                        unsigned bits = open[i] & allowed[quarter]; for (int l = 0; l < CL; l++) if ((bits >> l & 1) && wet[i][l] > most[quarter][l]) bits &= ~(1u << l);
                        cell[i] = (uint8_t) bits; seen[i] = 0; iceBits[i] = 0; if (!bits) continue;
                        points += __builtin_popcount(bits);
                        for (int a = std::max(0, (di - 2) / 2); a <= std::min(7, (di + 2) / 2); a++) for (int b = std::max(0, (dj - 2) / 2); b <= std::min(7, (dj + 2) / 2); b++) wanted |= 1ULL << (a * 8 + b);
                    }
                    lap(1);
                    if (points < 8) continue;
                    wanted |= 0x00003C3C3C3C0000ULL;   // (the middle 4 x 4 of the weirdness grid always: the index uses it)
                    for (int k = 0; k < 16; k++) tq[k] = (float) firstOctaves(&biomeNoise.climate[NP_TEMPERATURE], floor((x + side * (2 * (k / 4) + 1) / 8) / 4), floor((z + side * (2 * (k % 4) + 1) / 8) / 4), 99);
                    if (!lookTables) for (int k = 0; k < 64; k++) wq[k] = (wanted >> k & 1) ? (float) firstOctaves(&biomeNoise.climate[NP_WEIRDNESS], floor((x + side * (2 * (k / 8) + 1) / 16) / 4), floor((z + side * (2 * (k % 8) + 1) / 16) / 4), 2) : 0.0f;
                    else {   // all three octaves from the tables: own at the step itself, wide between the two even steps about an odd one
                        auto mod = [](long v, int n) { return (int) (((v % n) + n) % n); };
                        const long ei = (q.gi + 1) >> 1, ej = (q.gj + 1) >> 1; const bool betweenI = !(q.gi & 1), betweenJ = !(q.gj & 1);   // step gi + 2a + 1: even step 2 (ei + a) when gi is odd, else between 2 (ei + a) and 2 (ei + a + 1)
                        for (int k = 0; k < 64; k++) { if (!(wanted >> k & 1)) { wq[k] = 0; continue; } const int a = k / 8, b = k % 8; float v = 0;
                            for (int half = 0; half < 2; half++) { const int n = half ? NB : NA, oi = half ? ib : ia, oj = half ? jb : ja; const float *wp = widePoint[half].data();
                                v += ownPoint[half][(size_t) ((oi + 2 * a + 1) % n) * n + (oj + 2 * b + 1) % n];
                                const int i0 = mod(ei + a, n), j0 = mod(ej + b, n), i1 = betweenI ? (i0 + 1) % n : i0, j1 = betweenJ ? (j0 + 1) % n : j0;
                                v += 0.25f * (wp[(size_t) i0 * n + j0] + wp[(size_t) i0 * n + j1] + wp[(size_t) i1 * n + j0] + wp[(size_t) i1 * n + j1]); }
                            wq[k] = v; }
                        if (lookCheck) for (int k = 0; k < 64; k++) if (wanted >> k & 1) { const double d = wq[k] - firstOctaves(&biomeNoise.climate[NP_WEIRDNESS], (q.gi + 2 * (k / 8) + 1) * STEP / 4, (q.gj + 2 * (k % 8) + 1) * STEP / 4, 99); checkSum[t] += d * d; checkCount[t]++; checkMost[t] = std::max(checkMost[t], fabs(d)); }
                    }
                    q.tC = (tq[5] + tq[6] + tq[9] + tq[10]) / 4; q.cC = (cq[5] + cq[6] + cq[9] + cq[10]) / 4;
                    { float sum8 = 0, sum4 = 0; for (int a = 2; a < 6; a++) for (int b = 2; b < 6; b++) { sum8 += wq[a * 8 + b]; if (a >= 3 && a < 5 && b >= 3 && b < 5) sum4 += wq[a * 8 + b]; } q.w8 = sum8 / 16; q.w4 = sum4 / 4; }
                    // per column with air, or next to one: the heights in ice caves (nearer to its box than to a surface biome's)
                    auto over = [](float value, float lo, float hi) { const float d = value > hi ? value - hi : value < lo ? lo - value : 0; return d * d; };
                    for (int di = 0; di < WINDOW; di++) for (int dj = 0; dj < WINDOW; dj++) {
                        const int i = di * WINDOW + dj, quarter = (di / 4) * 4 + dj / 4;
                        if (!(cell[i] | (dj > 0 ? cell[i - 1] : 0) | (dj + 1 < WINDOW ? cell[i + 1] : 0) | (di > 0 ? cell[i - WINDOW] : 0) | (di + 1 < WINDOW ? cell[i + WINDOW] : 0))) continue;
                        const float fu = std::min(3.0f, std::max(0.0f, (di - 2) / 4.0f)), fv = std::min(3.0f, std::max(0.0f, (dj - 2) / 4.0f)); const int u = std::min(2, (int) fu), v = std::min(2, (int) fv); const float pu = fu - u, pv = fv - v;
                        auto mix = [&](const float *f) { return (f[u * 4 + v] * (1 - pu) + f[(u + 1) * 4 + v] * pu) * (1 - pv) + (f[u * 4 + v + 1] * (1 - pu) + f[(u + 1) * 4 + v + 1] * pu) * pv; };
                        const float fa = std::min(7.0f, std::max(0.0f, (di - 1) / 2.0f)), fb = std::min(7.0f, std::max(0.0f, (dj - 1) / 2.0f)); const int a = std::min(6, (int) fa), b = std::min(6, (int) fb); const float pa = fa - a, pb = fb - b;
                        const float w = (wq[a * 8 + b] * (1 - pa) + wq[(a + 1) * 8 + b] * pa) * (1 - pb) + (wq[a * 8 + b + 1] * (1 - pa) + wq[(a + 1) * 8 + b + 1] * pa) * pb;
                        const float far2 = over(mix(tq), -1.0f, -0.8f) + over(mix(cq), -1.05f, -0.19f) + over(w, 0.5f, 1.1f);
                        if (far2 >= 0.25f) continue;
                        for (int l = 0; l < CL; l++) {   // a point stands for the 16 blocks around its height (the lowest: down to the floor); ice caves if the cells at both ends are
                            const float low = depth0[quarter] - std::max(levelY(l) - 8, -56) / 128.0f, high = depth0[quarter] - (levelY(l) + 4) / 128.0f, nearLow = std::min(low, 1 - low), nearHigh = std::min(high, 1 - high);
                            if (nearLow > 0 && nearHigh > 0 && far2 + over(low, 0.2f, 0.9f) < nearLow * nearLow && far2 + over(high, 0.2f, 0.9f) < nearHigh * nearHigh) iceBits[i] |= 1u << l;
                        }
                    }
                    lap(2);
                    for (int start = 0; start < 256; start++) for (unsigned left; (left = cell[start] & ~seen[start]) != 0;) {
                        const unsigned first = fill(left & -left, cell[start]); seen[start] |= first; int count = __builtin_popcount(first), inIce = __builtin_popcount(first & iceBits[start]), edge = 0; stack.push_back({start, first});
                        while (!stack.empty()) {
                            const int i = stack.back().first; const unsigned bits = stack.back().second; stack.pop_back(); const int di = i / WINDOW, dj = i % WINDOW;
                            auto visit = [&](int j) { edge += __builtin_popcount(bits & ~cell[j] & ~iceBits[j]); unsigned fresh = bits & cell[j] & ~seen[j]; if (!fresh) return; fresh = fill(fresh, cell[j]) & ~seen[j]; seen[j] |= fresh; count += __builtin_popcount(fresh); inIce += __builtin_popcount(fresh & iceBits[j]); stack.push_back({j, fresh}); };
                            if (dj > 0) visit(i - 1); if (dj + 1 < WINDOW) visit(i + 1); if (di > 0) visit(i - WINDOW); if (di + 1 < WINDOW) visit(i + WINDOW);
                        }
                        if (inIce == count && edge == 0 && count > q.coarseSealed) q.coarseSealed = count;
                        if (inIce == count && count > q.coarsePure) q.coarsePure = count;
                        if (inIce > q.coarseIce) { q.coarseIce = inIce; q.coarseTotal = count; }
                    }
                    lap(3);
                }
            }
        });
        for (auto &t : lookers) t.join();
        double sum[4] = {0}; for (auto &v : lookSpent) for (int k = 0; k < 4; k++) sum[k] += v[k];
        fprintf(stderr, "first look: the pairs' columns %.2f s, sea floors and air points %.2f s, ice caves %.2f s, clusters %.2f s\n", sum[0], sum[1], sum[2], sum[3]);
        if (lookCheck) { double q2 = 0, most = 0; size_t n = 0; for (int t = 0; t < threads; t++) { q2 += checkSum[t]; n += checkCount[t]; most = std::max(most, checkMost[t]); } fprintf(stderr, "first look weirdness from the tables against sampled: rms %.4f, largest %.4f over %zu points\n", std::sqrt(q2 / std::max<size_t>(1, n)), most, n); }
    }
    if (haveIndex) parallel(threads, (int) places.size(), [&](int i) { Place &q = places[i]; if (q.coarseTotal == 0) return; float x[64]; featuresOf(q, x); double v = intercept; for (int k = 0; k < NF; k++) v += coefficient[k] * x[k]; q.index = (float) v; });
    const double lookDone = seconds();
    int lookBest = 0, lookOver40 = 0;   // the seed's best first-look score (max of 2 x sealed and pure, coarse points of 67,000 blocks) and its places from 40
    for (const Place &q : places) { const int worth = std::max(2 * q.coarseSealed, q.coarsePure); lookBest = std::max(lookBest, worth); lookOver40 += worth >= 40; }
    // The fine look goes to the wantPlaces best by max(coarseSealed, coarsePure / 2) and the wantIce best by coarseIce.
    // (Measured on 24 seeds with every place looked at closely: the 1,000 best of 26,000 hold 63 to 77% of the places whose
    // fine cluster is 160 points of ice caves throughout with no edge, and 58 to 74% of those with 200 points and any
    // edge; the 500 best by coarseIce 72% of the places with 900 points of ice caves in one cluster. The open points of
    // the pair, the cave search's order, held 12% and 25%.)
    // LOOK_ICE_MIN=n: only places whose coarse ice cluster has n points or more get the fine look (for the largest caverns
    // only: the 49 caverns of 4.7M+ known on 7 October had 39 to 106 such points, a point being 67,000 blocks).
    if (getenv("LOOK_ICE_MIN")) {
        const int least = atoi(getenv("LOOK_ICE_MIN")); size_t out = 0;
        for (size_t i = 0; i < places.size(); i++) if (places[i].coarseIce >= least) places[out++] = places[i];
        fprintf(stderr, "first look: %zu of %zu places have a coarse ice cluster of %d+ points\n", out, places.size(), least); places.resize(out);
    }
    if (places.size() > wantPlaces + wantIce) {
        std::vector<Place> chosen; std::vector<uint8_t> taken(places.size(), 0); std::vector<int> order(places.size());
        for (int by = 0; by < 2; by++) {
            for (size_t i = 0; i < order.size(); i++) order[i] = (int) i;
            const size_t want = by ? wantIce : wantPlaces; if (want == 0) continue;
            // (L1_NEAR percent: a coarse ice cluster with at most that share outside the ice caves counts like a pure one)
            auto worth = [&](int x) { const Place &q = places[x]; return std::max(std::max(2 * q.coarseSealed, q.coarsePure), (q.coarseTotal - q.coarseIce) * 100 <= lookNear * q.coarseTotal ? q.coarseIce : 0); };
            auto better = [&](int x, int y) { return by ? places[x].coarseIce > places[y].coarseIce : haveIndex ? places[x].index > places[y].index : worth(x) > worth(y); };
            std::nth_element(order.begin(), order.begin() + std::min(want, order.size() - 1), order.end(), better);
            for (size_t i = 0; i < want && i < order.size(); i++) if (!taken[order[i]]) { taken[order[i]] = 1; chosen.push_back(places[order[i]]); }
        }
        places.swap(chosen);
    }
    std::sort(places.begin(), places.end(), [](const Place &x, const Place &y) { return x.pair < y.pair; });
    std::vector<size_t> groups; // where each pair's places start
    for (size_t i = 0; i < places.size(); i++) if (i == 0 || places[i].pair != places[i - 1].pair) groups.push_back(i);
    groups.push_back(places.size());
    double terrainDone = seconds();

    // 5. the fine tables, where the windows of the pairs of those places are: values [slot][2 x 2 half steps][height]
    struct FineTable { std::vector<int> slot; std::vector<float> cheese, layer, flood; } fine[2];
    size_t fineColumns = 0;
    for (int half = 0; half < 2; half++) {
        FineTable &f = fine[half]; int n = table[half].n; f.slot.assign((size_t) n * n, -1);
        std::vector<uint8_t> top((size_t) n * n, 0); // the highest fine height a column needs: its places' rough surface, and 40 blocks to spare
        for (size_t g = 0; g + 1 < groups.size(); g++) {
            const Pair &p = pairs[places[groups[g]].pair]; int origin = half ? p.b : p.a, i0 = origin / n, j0 = origin % n;
            float highest = 0;
            for (size_t at = groups[g]; at < groups[g + 1]; at++) highest = std::max(highest, places[at].surface);
            int need = std::min(FINE_LEVELS - 1, std::max(1, (int) ((highest + 40 - MARGIN + 56) / 8)));
            for (int di = 0; di < WINDOW; di++) for (int dj = 0; dj < WINDOW; dj++) { size_t c = (size_t) ((i0 + di) % n) * n + (j0 + dj) % n; f.slot[c] = 0; top[c] = std::max(top[c], (uint8_t) need); }
        }
        std::vector<int> columns;
        for (int c = 0; c < n * n; c++) if (f.slot[c] == 0) { f.slot[c] = (int) columns.size(); columns.push_back(c); }
        fineColumns += columns.size();
        f.cheese.resize(columns.size() * 4 * FINE_LEVELS); f.layer.resize(f.cheese.size()); f.flood.resize(f.cheese.size());
        parallel(threads, (int) columns.size(), [&](int s) {
            int i = columns[s] / n, j = columns[s] % n;
            for (int sub = 0; sub < 4; sub++) {
                double x = (i + 0.5 * (sub >> 1)) * STEP, z = (j + 0.5 * (sub & 1)) * STEP; size_t at = ((size_t) s * 4 + sub) * FINE_LEVELS;
                int made = top[columns[s]] + 1;
                for (int l = 0; l < FINE_LEVELS; l++) { f.cheese[at + l] = l < made ? 0.0f : 1.0f; f.layer[at + l] = f.flood[at + l] = 0; } // above what is needed: closed
                addColumn(cheese, half, fineOctaves, x, z, 0.6666666666666666, FINE_Y0, 8, made, &f.cheese[at]); addColumn(layer, half, 99, x, z, 8.0, FINE_Y0, 8, made, &f.layer[at]); addColumn(flood, half, 99, x, z, 0.67, FINE_Y0, 8, made, &f.flood[at]);
            }
        });
    }
    double fineDone = seconds();

    // 6. the score of each of those places: its largest cluster on the fine grid
    struct Site { long gi, gj; int score, open, inner; float surface; int free; int total, quarters, pureAt, iceAt; int pair, preRank, edge; Place place; double worth; int freeEdge; };   // score: the pure cluster's points; worth: those weighted by its edge
    std::vector<std::vector<Site>> found(threads);
    std::atomic<size_t> nextGroup(0); std::vector<std::thread> pool;
    auto fillY = [](uint64_t reached, uint64_t open) { // every bit of open joined to a bit of reached along the column
        uint64_t up = reached, down = reached, pass = open;
        for (int k = 1; k < 64; k <<= 1) { up |= pass & (up << k); pass &= pass << k; }
        pass = open;
        for (int k = 1; k < 64; k <<= 1) { down |= pass & (down >> k); pass &= pass >> k; }
        return up | down;
    };
    std::vector<std::array<double, 6>> spent(threads, std::array<double, 6>{0, 0, 0, 0, 0, 0}); std::atomic<size_t> closerLooks(0);
    for (int t = 0; t < threads; t++) pool.emplace_back([&, t] {
        auto tick = [] { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }; double mark = tick();
        auto lap = [&](int k) { const double now = tick(); spent[t][k] += now - mark; mark = now; };
        std::vector<uint64_t> levelsOpen(FINE * FINE), cell(FINE * FINE), seen(FINE * FINE), deepCell(FINE * FINE); std::vector<std::pair<int, uint64_t>> stack; std::vector<float> floodAt((size_t) FINE * FINE * FINE_LEVELS);
        for (size_t g; (g = nextGroup++) + 1 < groups.size();) {
            const Pair &p = pairs[places[groups[g]].pair]; int ia = p.a / NA, ja = p.a % NA, ib = p.b / NB, jb = p.b % NB;
            // the open-and-dry heights of every fine column, a bit per height. Near the bottom of the world the terrain's slide
            // closes what is not well open: the density is 0.1171875 (1 - f) + f x the cave value, f = (y + 64) / 24 up to y -40.
            for (int di = 0; di < WINDOW; di++) for (int dj = 0; dj < WINDOW; dj++) {
                size_t sa = (size_t) fine[0].slot[(size_t) ((ia + di) % NA) * NA + (ja + dj) % NA] * 4 * FINE_LEVELS, sb = (size_t) fine[1].slot[(size_t) ((ib + di) % NB) * NB + (jb + dj) % NB] * 4 * FINE_LEVELS;
                for (int sub = 0; sub < 4; sub++) {
                    const float *ca = &fine[0].cheese[sa + sub * FINE_LEVELS], *cb = &fine[1].cheese[sb + sub * FINE_LEVELS], *la = &fine[0].layer[sa + sub * FINE_LEVELS], *lb = &fine[1].layer[sb + sub * FINE_LEVELS],
                                *fa = &fine[0].flood[sa + sub * FINE_LEVELS], *fb = &fine[1].flood[sb + sub * FINE_LEVELS];
                    uint64_t bits = 0;
                    for (int l = 0; l < FINE_MADE; l++) {
                        float c = ca[l] + cb[l] + 0.27f; const float f = std::min(1.0f, (fineY(l) + 64) / 24.0f), slide = -0.1171875f * (1 - f) / f;
                        if (c >= slide) continue;
                        if (c < -1) c = -1;
                        float ly = la[l] + lb[l];
                        if (c + 4 * ly * ly < slide && fa[l] + fb[l] <= 0.4f) bits |= 1ULL << l;   // (dry under land; a place under the sea asks for less below)
                    }
                    levelsOpen[(size_t) (2 * di + (sub >> 1)) * FINE + 2 * dj + (sub & 1)] = bits;
                    for (int l = 0; l < FINE_MADE; l++) floodAt[((size_t) (2 * di + (sub >> 1)) * FINE + 2 * dj + (sub & 1)) * FINE_LEVELS + l] = fa[l] + fb[l];
                }
            }
            lap(0);
            for (size_t at = groups[g]; at < groups[g + 1]; at++) {
                const Place &q = places[at];
                // the sea floor (or the ground) of each quarter x quarter of the window, and the heights that count under it
                uint64_t under[16]; float dryMax[16][FINE_LEVELS]; double surface = 0, x = q.gi * STEP, z = q.gj * STEP, side = WINDOW * STEP;
                for (int u = 0; u < 4; u++) for (int v = 0; v < 4; v++) {
                    const int qx = (int) floor((x + side * (2 * u + 1) / 8) / 4), qz = (int) floor((z + side * (2 * v + 1) / 8) / 4);
                    IceClimate c; c.c = (float) firstOctaves(&biomeNoise.climate[NP_CONTINENTALNESS], qx, qz, 99); c.e = c.w = 0;
                    if (c.c > -0.2f) { c.e = (float) firstOctaves(&biomeNoise.climate[NP_EROSION], qx, qz, 99); c.w = (float) firstOctaves(&biomeNoise.climate[NP_WEIRDNESS], qx, qz, 99); }   // (the sea floor depends on the continentalness alone)
                    float par[] = {c.c, c.e, -3.0F * (fabsf(fabsf(c.w) - 0.6666667F) - 0.33333334F), c.w};
                    const double here = 128.0 * (1.0 - 83.0 / 160.0 + getSpline(biomeNoise.sp, par) + 0.015F); surface += here / 16;
                    const bool sea = here < 63; uint64_t allowed = 0;
                    for (int l = 0; l < FINE_MADE; l++) {
                        const int y = fineY(l);
                        if (y <= here - (sea ? 8 : 24)) allowed |= 1ULL << l;
                        // Aquifer.computeSurfaceLevel: no fluid under this floodedness; under a sea floor it falls from 0.4 at 64 blocks below to -0.8 at the floor
                        dryMax[u * 4 + v][l] = sea ? (float) (0.4 - 1.2 * std::min(1.0, std::max(0.0, 1.0 - (here + 8 - y) / 64.0))) : 0.4f;
                    }
                    under[u * 4 + v] = allowed;
                }
                lap(1);
                int points = 0;
                for (int di = 0; di < FINE; di++) for (int dj = 0; dj < FINE; dj++) {
                    const int quarter = (di / 8) * 4 + dj / 8; uint64_t bits = levelsOpen[di * FINE + dj] & under[quarter];
                    for (uint64_t left = bits; left; left &= left - 1) { const int l = __builtin_ctzll(left); if (floodAt[((size_t) di * FINE + dj) * FINE_LEVELS + l] > dryMax[quarter][l]) bits &= ~(1ULL << l); }
                    cell[di * FINE + dj] = bits; seen[di * FINE + dj] = 0; points += __builtin_popcountll(bits);
                }
                lap(2);
                // The ice caves, column by column: temperature, humidity and continentalness from 9 x 9 nodes (they change
                // over hundreds of blocks), the weirdness at every column that has air or stands next to one that has.
                // A column's ice caves heights follow from its distance to the box; a point stands for the 8 blocks around
                // its height and is ice caves only if the cells at both ends of those are.
                {
                    float node[81][4];
                    for (int a = 0; a < 9; a++) for (int b = 0; b < 9; b++) {
                        const int nx = (int) floor((x + side * a / 8) / 4), nz = (int) floor((z + side * b / 8) / 4); float *n = node[a * 9 + b];
                        n[0] = (float) firstOctaves(&biomeNoise.climate[NP_TEMPERATURE], nx, nz, 99); n[1] = (float) firstOctaves(&biomeNoise.climate[NP_HUMIDITY], nx, nz, 99); n[2] = (float) firstOctaves(&biomeNoise.climate[NP_CONTINENTALNESS], nx, nz, 99);
                        n[3] = n[2] > -0.2f ? (float) firstOctaves(&biomeNoise.climate[NP_EROSION], nx, nz, 99) : 0.0f;   // (under the sea the floor does not depend on the erosion)
                    }
                    for (int di = 0; di < FINE; di++) for (int dj = 0; dj < FINE; dj++) {
                        const int i = di * FINE + dj;
                        if (!(cell[i] | (dj > 0 ? cell[i - 1] : 0) | (dj + 1 < FINE ? cell[i + 1] : 0) | (di > 0 ? cell[i - FINE] : 0) | (di + 1 < FINE ? cell[i + FINE] : 0))) { deepCell[i] = ~0ULL; continue; }
                        const double fx = (di + 0.5) * 8.0 / FINE, fz = (dj + 0.5) * 8.0 / FINE; const int a = std::min(7, (int) fx), b = std::min(7, (int) fz); const float u = (float) (fx - a), v = (float) (fz - b);
                        float m[4]; for (int k = 0; k < 4; k++) m[k] = (node[a * 9 + b][k] * (1 - u) + node[(a + 1) * 9 + b][k] * u) * (1 - v) + (node[a * 9 + b + 1][k] * (1 - u) + node[(a + 1) * 9 + b + 1][k] * u) * v;
                        const int qx = (int) floor((x + (di + 0.5) * STEP / 2) / 4), qz = (int) floor((z + (dj + 0.5) * STEP / 2) / 4);
                        const float w = (float) firstOctaves(&biomeNoise.climate[NP_WEIRDNESS], qx, qz, 99);
                        float par[] = {m[2], m[3], -3.0F * (fabsf(fabsf(w) - 0.6666667F) - 0.33333334F), w}; const double off = getSpline(biomeNoise.sp, par) + 0.015F;
                        // squared distance to the ice caves box, and to the nearest other cave biome's (lush: humidity; dripstone: continentalness)
                        auto over = [](float value, float lo, float hi) { const float d = value > hi ? value - hi : value < lo ? lo - value : 0; return d * d; };
                        const float ice = over(m[0], -1.0f, -0.8f) + over(m[1], -1.0f, 0.7f) + over(m[2], -1.05f, -0.19f) + over(w, 0.5f, 1.1f);
                        const float other = std::min(over(m[1], 0.7f, 1.0f), over(m[1], -1.0f, 0.7f) + over(m[2], 0.8f, 1.0f));
                        uint64_t notIce = ~0ULL;
                        if (ice < other && ice < 0.25f) {
                            // ice caves where the depth d has ice + (d outside 0.2 .. 0.9)^2 < min(d, 1 - d)^2; a cell's depth is that of its lowest block
                            const double root = sqrt((double) ice), lo = ice < 0.04f ? (ice + 0.04) / 0.4 : root, hi = ice < 0.01f ? (0.19 - ice) / 0.2 : 1 - root;
                            const double yTop = 128.0 * (1.0 - 83.0 / 160.0 + off - lo), yBottom = 128.0 * (1.0 - 83.0 / 160.0 + off - hi);
                            for (int l = 0; l < FINE_MADE; l++) { const int y = fineY(l), low = (std::max(y - 4, -56) >> 2) * 4, high = ((y + 3) >> 2) * 4; if (high < yTop && low > yBottom) notIce &= ~(1ULL << l); }
                        }
                        deepCell[i] = notIce;
                    }
                }
                lap(4);
                // Clusters. best: the largest that is ice caves throughout, weighted by its edge (the sideways neighbours of its
                // points that are rock or water outside the ice caves, or past the window: where the cavern between the points
                // can leave the ice caves; measured with the formula's clusters: 84% of those with no such neighbour are ice
                // caves throughout in the game, 45% with a few, 30% with more). bestFree of bestTotal: the most ice caves points.
                int best = 0, bestEdge = 0, bestFree = 0, bestTotal = 0, freeEdge = 0, bestAt = -1, freeAt = -1; double bestWorth = 0;
                if (points >= minPoints) for (int start = 0; start < FINE * FINE; start++) {
                    for (uint64_t left; (left = cell[start] & ~seen[start]) != 0;) {
                        uint64_t first = fillY(left & -left, cell[start]); seen[start] |= first; int count = __builtin_popcountll(first), freeCount = __builtin_popcountll(first & ~deepCell[start]), edge = 0; stack.push_back({start, first});
                        long sumI = 0, sumJ = 0;
                        while (!stack.empty()) {
                            int i = stack.back().first; uint64_t bits = stack.back().second; stack.pop_back();
                            int dj = i % FINE, di = i / FINE; const int n = __builtin_popcountll(bits); sumI += (long) di * n; sumJ += (long) dj * n;
                            auto visit = [&](int j) {
                                edge += __builtin_popcountll(bits & ~cell[j] & deepCell[j]);
                                uint64_t fresh = bits & cell[j] & ~seen[j];
                                if (!fresh) return;
                                fresh = fillY(fresh, cell[j]) & ~seen[j]; seen[j] |= fresh; count += __builtin_popcountll(fresh); freeCount += __builtin_popcountll(fresh & ~deepCell[j]); stack.push_back({j, fresh});
                            };
                            if (dj > 0) visit(i - 1); else edge += n; if (dj + 1 < FINE) visit(i + 1); else edge += n;
                            if (di > 0) visit(i - FINE); else edge += n; if (di + 1 < FINE) visit(i + FINE); else edge += n;
                        }
                        const int middle = (int) (sumI / count) * FINE + (int) (sumJ / count);   // the column at the cluster's middle
                        const double worth = count * (edge == 0 ? 1.0 : edge < 6 ? 0.55 : 0.35);
                        if (freeCount == count && worth > bestWorth) { bestWorth = worth; best = count; bestEdge = edge; bestAt = middle; }
                        if (freeCount > bestFree) { bestFree = freeCount; bestTotal = count; freeEdge = edge; freeAt = middle; }
                    }
                }
                lap(5); closerLooks++;
                found[t].push_back({q.gi, q.gj, best, p.open, points, (float) surface, bestFree, bestTotal, q.quarters, bestAt, freeAt, q.pair, q.preRank, bestEdge, q, bestWorth, freeEdge});
            }
        }
    });
    for (auto &t : pool) t.join();
    std::vector<Site> sites;
    for (auto &f : found) sites.insert(sites.end(), f.begin(), f.end());
    { double sum[6] = {0}; for (auto &v : spent) for (int k = 0; k < 6; k++) sum[k] += v[k];
      fprintf(stderr, "fine look at %zu places: a pair's open heights %.2f s, sea floors %.2f s, air points %.2f s, ice caves columns %.2f s, clusters %.2f s\n", closerLooks.load(), sum[0], sum[1], sum[2], sum[4], sum[5]); }
    if (oracle && getenv("ORACLE")[0] == '/') {   // every place that was looked at closely, with what was known of it before
        FILE *f = fopen(getenv("ORACLE"), "w");
        for (const Site &s : sites) {
            fprintf(f, "%lld %ld %ld pure %d ice %d of %d coarse %d %d %d quarters %d floor %.0f mean %.4f meanrank %.4f select %d pre %d points %d edge %d %d", (long long) seed, s.gi, s.gj, s.score, s.free, s.total, s.place.coarsePure, s.place.coarseIce, s.place.coarseSealed,
                    s.quarters, s.place.floor, pairs[s.pair].meanCheese, meanRank[s.pair], (size_t) s.pair < firstPart ? selectA : selectB, s.place.pre, s.inner, s.edge, s.freeEdge);
            float x[64]; featuresOf(s.place, x); fprintf(f, " features");   // (in the order of FEATURES)
            for (int k = 0; k < NF; k++) fprintf(f, " %.4f", x[k]);
            fprintf(f, " index %.3f\n", s.place.index);
        }
        fclose(f);
    }
    // the best first by cluster, then by free, skipping windows that overlap one already printed by more than half
    std::vector<Site> kept, printed;
    for (int by = 0; by < 2; by++) {
        // (first list: by the pure cluster, or the ice cluster if at most NEAR_PCT (8) percent of it is outside the ice caves: the
        //  fine look's rule for ice caves is cautious, and a third of the caverns that are ice caves throughout in the game show a
        //  few points outside. Of 34 such caverns of 3M+ found with a limit of 3%, 12 had over 2% outside, and the largest, with
        //  3.7%, was only seen because it was also on the list by ice caves points.)
        const int nearPct = getenv("NEAR_PCT") ? atoi(getenv("NEAR_PCT")) : 8;
        auto pureLike = [nearPct](const Site &s) { return std::max(s.score, (s.total - s.free) * 100 <= nearPct * s.total ? s.free : 0); };
        std::sort(sites.begin(), sites.end(), [&](const Site &x, const Site &y) { return by ? x.free > y.free : pureLike(x) > pureLike(y); });
        std::vector<Site> list;
        for (const Site &s : sites) {
            if ((int) list.size() >= top) break;
            bool overlap = false;
            for (const Site &k : list) if (labs(k.gi - s.gi) < WINDOW / 2 + 1 && labs(k.gj - s.gj) < WINDOW / 2 + 1) { overlap = true; break; }
            if (overlap) continue;
            list.push_back(s);
            bool again = false;
            for (const Site &k : printed) if (k.gi == s.gi && k.gj == s.gj) { again = true; break; }
            if (again) continue;
            printed.push_back(s);
            auto columnX = [&](int at) { return at < 0 ? (s.gi + WINDOW / 2.0) * STEP : s.gi * STEP + (at / FINE + 0.5) * STEP / 2; };
            auto columnZ = [&](int at) { return at < 0 ? (s.gj + WINDOW / 2.0) * STEP : s.gj * STEP + (at % FINE + 0.5) * STEP / 2; };
            printf("%lld %.0f %.0f ice %d of %d pure %d floor %.0f points %d quarters %d ice_at %.0f %.0f pure_at %.0f %.0f", (long long) seed, (s.gi + WINDOW / 2.0) * STEP, (s.gj + WINDOW / 2.0) * STEP, s.free, s.total, s.score, s.surface, s.inner, s.quarters,
                   columnX(s.iceAt), columnZ(s.iceAt), columnX(s.pureAt), columnZ(s.pureAt));
            printf(" coarse %d %d %d edge %d %d", s.place.coarsePure, s.place.coarseIce, s.place.coarseTotal, s.edge, s.freeEdge);
            if (haveWide) printf(" wbox %.3f own %.3f cheese %.4f", s.place.wbox, s.place.own, pairs[s.pair].meanCheese);
            if (oracle) printf(" select %d mean %.4f meanrank %.4f pre %d prerank %d edge %d", (size_t) s.pair < firstPart ? selectA : selectB, pairs[s.pair].meanCheese, meanRank[s.pair], s.open, s.preRank, s.edge);
            printf("\n");
        }
        if (by == 0) kept = list;
    }
    fprintf(stderr, "seed %lld: tables %.1f s, %zu pairs with mean cheese <= %.3f (lowest %.3f) %.1f s, climate at %zu places (%.1f samples each, %zu under ice caves) %.1f s, their pairs' columns %.1f s, first look %.1f s, fine tables for %zu pairs (%zu columns, %.1f%%) %.1f s, "
            "clusters at %zu places %.1f s; best score %d; first look best %d over40 %d\n", (long long) seed, tablesDone, pairs.size(), usedThreshold, lowestMean, pairsDone - tablesDone, allPlaces.load(), (double) samples.load() / std::max<size_t>(1, allPlaces.load()), viablePlaces, climateDone - formulaDone, terrainDone - climateDone - (lookDone - lookStart), lookDone - lookStart,
            groups.size() - 1, fineColumns, 100.0 * fineColumns / ((double) NA * NA + (double) NB * NB), fineDone - terrainDone, places.size(), seconds() - fineDone, kept.empty() ? 0 : kept[0].score, lookBest, lookOver40);
}
