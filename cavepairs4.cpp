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
// build: g++ -O3 -march=native -std=c++17 -I vendor/cubiomes cavepairs4.cpp vendor/cub_*.o -lpthread -o cavepairs4
// Version 4: with GPU_TERRAIN=1 in the environment the terrain step (two thirds of a full scan) is done by gpusrv on the GPU
// (gputerrain.h); GPU_TERRAIN_CHECK=1 also redoes it here and reports the differences.
// usage: cavepairs3 SEED [PAIRS=600000] [TOP=300] [THREADS=8] [PLACES=15000] [MIN_CUT=0]     (STATS=1: only the tables' statistics)
// PLACES: how many places (the best by open points under the surface) get the fine tables and the cluster.
// MIN_CUT: stop early (no sites) if the cut of the terrain step, known a third of the way through, is lower.
#include "cavenoise.hpp"
extern "C" {
#include "biomenoise.h"
float getSpline(const Spline *sp, const float *vals);
}
#include <immintrin.h>
#include "gputerrain.h"
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
static bool readAll(int fd, void *buffer, size_t n) { char *p = (char *) buffer; while (n) { ssize_t k = read(fd, p, n); if (k <= 0) return false; p += k; n -= (size_t) k; } return true; }
static bool writeAll(int fd, const void *buffer, size_t n) { const char *p = (const char *) buffer; while (n) { ssize_t k = write(fd, p, n); if (k <= 0) return false; p += k; n -= (size_t) k; } return true; }
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <thread>
#include <unordered_set>
#include <vector>

static const int NA = 1011, NB = 993, LEVELS = 16, WINDOW = 16, MEAN_LEVELS = 10, MARGIN = 48;
static const int FINE_LEVELS = 33, FINE = 2 * WINDOW;   // the fine grid: half steps, y = -56 + 8 l
static int COARSE_OCTAVES = 4;                            // cheese octaves in the coarse tables (of 7: the three highest are 2% of the variance; env COARSE_OCTAVES)
static const int CUT_PLACES = 15000;                      // the early exit's cut is the open points of the place at 1.5 x this rank
static const int BASE_OCTAVES = 2;                        // of those, the ones every column gets (the pairs are picked on them)
static const int MADE_LEVELS = 12;                        // coarse heights that are made (y -40 .. 136): the ones above only count under a surface over 200, and the fine tables have them
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

// The coarse tables, a cache line per column: the formula step reads 600,000 x 256 x 2 of them, and memory is what it
// waits for. cheese in 1/4096, layer and floodedness in 1/64 (a height that is not made: cheese +1, which is closed).
struct alignas(64) Packed { int16_t cheese[16]; int8_t layer[16], flood[16]; };
struct Tables {
    int n;
    std::vector<Packed> columns;   // [i][j]
    std::vector<float> sum;        // [i][j]: the base cheese summed over the MEAN_LEVELS lowest heights
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
    const uint64_t seed = strtoull(argv[1], 0, 10); const size_t wantPairs = (argc > 2 ? strtoull(argv[2], 0, 10) : 600000) / 2; const int top = argc > 3 ? atoi(argv[3]) : 300, threads = argc > 4 ? atoi(argv[4]) : 8; const size_t wantPlaces = argc > 5 ? strtoull(argv[5], 0, 10) : 15000; const int minCut = argc > 6 ? atoi(argv[6]) : 0;
    if (getenv("COARSE_OCTAVES")) COARSE_OCTAVES = atoi(getenv("COARSE_OCTAVES"));
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
        t.columns.resize((size_t) t.n * t.n); t.sum.resize((size_t) t.n * t.n); t.complete.assign((size_t) t.n * t.n, 0);
        parallel(threads, t.n, [&](int i) {
            for (int j = 0; j < t.n; j++) {
                double x = i * STEP, z = j * STEP; Packed &p = t.columns[(size_t) i * t.n + j]; float sum = 0, c[LEVELS] = {0};
                addColumn(cheese, half, BASE_OCTAVES, x, z, 0.6666666666666666, levelY(0), 16, MADE_LEVELS, c);
                for (int k = 0; k < LEVELS; k++) {
                    p.layer[k] = p.flood[k] = 0;
                    if (k >= MADE_LEVELS) { p.cheese[k] = 4096; continue; } // closed
                    if (k < MEAN_LEVELS) sum += c[k];
                    p.cheese[k] = (int16_t) std::lrint(std::max(-3.9f, std::min(3.9f, c[k])) * 4096);
                }
                t.sum[(size_t) i * t.n + j] = sum;
            }
        });
    }
    double tablesDone = seconds();

    // 2. the mean cheese of every window (periodic), per stack; a pair is the window origins (A index, B index)
    struct Pair { int a, b; float meanCheese; int counts[LEVELS]; int open; bool sample; }; // sample: one of the sixteenth of the pairs with the lowest sums (see the terrain step)
    std::vector<Pair> pairs, parts[2]; float lowestMean = 0, usedThreshold = 0;
    auto selectPairs = [&](int select, std::vector<Pair> &pairs) {
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
    for (int a = 0; a < NA * NA; a++) {
        int room = lo - bucketOf(mean[0][a], lowA); // the B buckets this window may take
        if (room < 0) continue;
        int c = classOfA(a); const uint32_t *s = &startB[c * (BUCKETS + 1)];
        for (uint32_t k = classStart[c], end = classStart[c] + s[std::min(room, BUCKETS - 1) + 1]; k < end; k++) {
            int b = orderB[k];
            Pair p; p.a = ((a / NA - shift + NA) % NA) * NA + (a % NA - shift + NA) % NA; p.b = ((b / NB - shift + NB) % NB) * NB + (b % NB - shift + NB) % NB; p.meanCheese = mean[0][a] + mean[1][b]; pairs.push_back(p);
        }
    }
    { // the sample: the pairs under the threshold that gives a sixteenth as many
        int lo16 = 0, hi16 = lo;
        while (lo16 < hi16) { int mid = (lo16 + hi16) / 2; if (pairsUpTo(mid) >= wantPairs / 16) hi16 = mid; else lo16 = mid + 1; }
        const float sampleThreshold = lowest + (lo16 + 1) / perUnit;
        for (Pair &p : pairs) p.sample = p.meanCheese <= sampleThreshold;
    }
    if (select == 8) { lowestMean = lowest; usedThreshold = threshold; }
    };
    { std::thread other(selectPairs, 16, std::ref(parts[1])); selectPairs(8, parts[0]); other.join(); }
    pairs = std::move(parts[0]); pairs.insert(pairs.end(), parts[1].begin(), parts[1].end()); parts[1].clear(); parts[1].shrink_to_fit();
    double pairsDone = seconds();

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
    formula(true);
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
    int terrainFd = -1; // GPU_TERRAIN=1: the surface estimates come from gpusrv (gputerrain.h); without it, or if it does not answer, from this process
    if (getenv("GPU_TERRAIN")) {
        terrainFd = socket(AF_UNIX, SOCK_STREAM, 0); sockaddr_un address; memset(&address, 0, sizeof address); address.sun_family = AF_UNIX; strncpy(address.sun_path, TERRAIN_SOCKET, sizeof address.sun_path - 1);
        if (terrainFd >= 0 && connect(terrainFd, (sockaddr *) &address, sizeof address)) { close(terrainFd); terrainFd = -1; }
    }
    BiomeNoise biomeNoise; initBiomeNoise(&biomeNoise, MC_26_3); setBiomeSeed(&biomeNoise, seed, 0);
    struct Place { int pair; long gi, gj; float surface; int pre; };
    std::vector<std::vector<Place>> viable(threads); std::atomic<size_t> allPlaces(0), samples(0);
    // Two passes. The first, over the sample (the sixteenth of the pairs with the lowest sums: as telling as every 16th
    // pair and on half as many table columns), shows how many open points under the surface the best places have (the cut). In the second a pair needs a surface high enough for its own points to reach the cut: most
    // pairs need more than the terrain around them has, and one or two samples say so.
    int shownCut = 0, cut = minCut; // with a minimum cut the first pass only has to show whether the seed reaches it: places that cannot have that many points go early
    for (int pass = 0; pass < 2; pass++) {
        if (terrainFd >= 0) { // the terrain step on the GPU (gpusrv): every place of the pass in one request
            std::vector<TerrainPlace> list; std::vector<int> pairOf; const double listStart = seconds();
            for (size_t index = 0; index < pairs.size(); index++) {
                if (pairs[index].sample != (pass == 0)) continue;
                const Pair &p = pairs[index]; int ia = p.a / NA, ja = p.a % NA, ib = p.b / NB, jb = p.b % NB;
                double need = SURFACE_MEAN;
                if (cut > 0) {
                    int sum = 0, k = 0;
                    for (; k < LEVELS; k++) { sum += p.counts[k]; if (sum >= cut) break; }
                    if (k == LEVELS) continue;
                    need = std::max(SURFACE_MEAN, (double) levelY(k) + MARGIN);
                }
                auto solve = [](int a, int b) { int d = (((b - a) / 3) % 331 + 331) % 331; return (long) a + 1011L * ((d * 276) % 331); };
                long gi0 = solve(ia, ib), gj0 = solve(ja, jb);
                for (long gi = gi0 - 2L * COMMON; gi <= gi0 + COMMON; gi += COMMON) for (long gj = gj0 - 2L * COMMON; gj <= gj0 + COMMON; gj += COMMON) {
                    double x = gi * STEP, z = gj * STEP, side = WINDOW * STEP;
                    if (x < -BORDER || z < -BORDER || x + side > BORDER || z + side > BORDER) continue;
                    TerrainPlace q = {(int32_t) gi, (int32_t) gj, (float) need}; list.push_back(q); pairOf.push_back((int) index);
                }
            }
            std::vector<float> surface(list.size()); const double askStart = seconds();
            for (size_t done = 0; done < list.size();) { // in requests of at most 2 million places
                TerrainRequest request = {TERRAIN_MAGIC, (uint32_t) std::min<size_t>(list.size() - done, 2000000), seed};
                if (!writeAll(terrainFd, &request, sizeof request) || !writeAll(terrainFd, list.data() + done, (size_t) request.count * sizeof(TerrainPlace)) || !readAll(terrainFd, surface.data() + done, (size_t) request.count * sizeof(float))) { fprintf(stderr, "seed %lld: the terrain service failed\n", (long long) seed); return 1; }
                done += request.count;
            }
            if (getenv("GPU_TERRAIN_TIMES")) fprintf(stderr, "seed %lld pass %d: %zu places listed in %.3f s, answered in %.3f s\n", (long long) seed, pass, list.size(), askStart - listStart, seconds() - askStart);
            if (getenv("GPU_TERRAIN_CHECK")) { // the same places on the CPU, for comparison
                size_t both = 0, onlyGpu = 0, onlyCpu = 0; double worst = 0;
                for (size_t i = 0; i < list.size(); i++) {
                    double x = list[i].gi * STEP, z = list[i].gj * STEP, side = WINDOW * STEP, need = list[i].need, sum = 0; bool have[16] = {false}, in = false;
                    auto sample = [&](int u, int v) { sum += surfaceEstimate(&biomeNoise, (int) floor((x + side * (2 * u + 1) / 8) / 4), (int) floor((z + side * (2 * v + 1) / 8) / 4), 3, 2, 2); have[u * 4 + v] = true; };
                    sample(0, 1);
                    if (sum >= need - 40) { sample(3, 2); if (sum >= 2 * (need - 25)) { sample(1, 3); sample(2, 0); if (sum >= 4 * std::max(SURFACE_FIRST, need - 12)) { for (int u = 0; u < 4; u++) for (int v = 0; v < 4; v++) if (!have[u * 4 + v]) sample(u, v); in = sum >= 16 * need; } } }
                    bool g = surface[i] >= 0; both += g && in; onlyGpu += g && !in; onlyCpu += in && !g;
                    if (g && in) worst = std::max(worst, std::fabs(surface[i] - sum / 16));
                }
                fprintf(stderr, "seed %lld pass %d: %zu places; in on both %zu, on the GPU only %zu, on the CPU only %zu; largest surface difference %.4f blocks\n", (long long) seed, pass, list.size(), both, onlyGpu, onlyCpu, worst);
            }
            for (size_t i = 0; i < list.size(); i++) if (surface[i] >= 0) {
                Place q; q.pair = pairOf[i]; q.gi = list[i].gi; q.gj = list[i].gj; q.surface = surface[i]; q.pre = 0; const Pair &p = pairs[q.pair];
                for (int k = 0; k < LEVELS; k++) if (levelY(k) <= q.surface - MARGIN) q.pre += p.counts[k];
                viable[0].push_back(q);
            }
            allPlaces += list.size(); samples += 4 * list.size();
        } else {
            std::atomic<size_t> nextPair(0); std::vector<std::thread> pool;
            for (int t = 0; t < threads; t++) pool.emplace_back([&, t] {
                size_t count = 0, taken = 0;
                for (size_t index; (index = nextPair++) < pairs.size();) {
                    if (pairs[index].sample != (pass == 0)) continue;
                    const Pair &p = pairs[index]; int ia = p.a / NA, ja = p.a % NA, ib = p.b / NB, jb = p.b % NB;
                    double need = SURFACE_MEAN; // the surface under which this pair has cut open points
                    if (cut > 0) {
                        int sum = 0, k = 0;
                        for (; k < LEVELS; k++) { sum += p.counts[k]; if (sum >= cut) break; }
                        if (k == LEVELS) continue;
                        need = std::max(SURFACE_MEAN, (double) levelY(k) + MARGIN);
                    }
                    // g = ia + 1011 k with 18 k = ib - ia (mod 993): k = ((ib - ia) / 3) * 276 (mod 331), 276 being 1 / 6 mod 331
                    auto solve = [](int a, int b) { int d = (((b - a) / 3) % 331 + 331) % 331; return (long) a + 1011L * ((d * 276) % 331); };
                    long gi0 = solve(ia, ib), gj0 = solve(ja, jb);
                    for (long gi = gi0 - 2L * COMMON; gi <= gi0 + COMMON; gi += COMMON) for (long gj = gj0 - 2L * COMMON; gj <= gj0 + COMMON; gj += COMMON) {
                        double x = gi * STEP, z = gj * STEP, side = WINDOW * STEP;
                        if (x < -BORDER || z < -BORDER || x + side > BORDER || z + side > BORDER) continue;
                        count++;
                        // the rough surface estimate of each quarter x quarter of the window; 1, then 2, then 4 of the 16 first
                        bool have[16] = {false}; double sum = 0;
                        auto sample = [&](int u, int v) { sum += surfaceEstimate(&biomeNoise, (int) floor((x + side * (2 * u + 1) / 8) / 4), (int) floor((z + side * (2 * v + 1) / 8) / 4), 3, 2, 2); have[u * 4 + v] = true; taken++; };
                        sample(0, 1);
                        if (sum < need - 40) continue;
                        sample(3, 2);
                        if (sum < 2 * (need - 25)) continue;
                        sample(1, 3); sample(2, 0);
                        if (sum < 4 * std::max(SURFACE_FIRST, need - 12)) continue;
                        for (int u = 0; u < 4; u++) for (int v = 0; v < 4; v++) if (!have[u * 4 + v]) sample(u, v);
                        if (sum < 16 * need) continue;
                        Place q; q.pair = (int) index; q.gi = gi; q.gj = gj; q.surface = (float) (sum / 16); q.pre = 0;
                        for (int k = 0; k < LEVELS; k++) if (levelY(k) <= q.surface - MARGIN) q.pre += p.counts[k];
                        viable[t].push_back(q);
                    }
                }
                allPlaces += count; samples += taken;
            });
            for (auto &t : pool) t.join();
        }
        if (pass == 0) { // the cut: what the place at 1.5 x the share of PLACES has
            std::vector<int> pre;
            for (auto &v : viable) for (const Place &q : v) pre.push_back(q.pre);
            // (the cut of the early exit is always that of 15,000 places, whatever PLACES is: MIN_CUT is calibrated on it)
            // The second pass only needs places that can be among the best PLACES: at least as good as the sample's own
            // place of that rank (the sample is the best pairs, so the place of that rank over all pairs is no worse); if
            // the sample kept fewer (it was pruned with MIN_CUT), at least MIN_CUT.
            size_t rank = CUT_PLACES / 16 * 3 / 2;
            cut = 0; int keep = minCut;
            if (pre.size() > rank) { std::nth_element(pre.begin(), pre.begin() + rank, pre.end(), std::greater<int>()); cut = pre[rank]; }
            if (pre.size() > wantPlaces) { std::nth_element(pre.begin(), pre.begin() + wantPlaces, pre.end(), std::greater<int>()); keep = pre[wantPlaces]; }
            // The cut is also the best early sign of what the seed holds: it says how many places have low noise AND tall
            // terrain over them, which no seed-level filter knows. Over 600 gated seeds one standard deviation of it is a
            // factor 2.7 in formula caverns of 25M+ (3.5 for 27M+); the top 15% of seeds by cut hold 60-68% of them.
            if (cut < minCut) {
                fprintf(stderr, "seed %lld: tables %.2f s, %zu pairs with mean cheese <= %.3f (lowest %.3f) %.2f s, %zu columns completed, cut %d under %d: stopped after %.2f s\n", (long long) seed, tablesDone, pairs.size(), usedThreshold, lowestMean,
                        pairsDone - tablesDone, completed, cut, minCut, seconds());
                return 0;
            }
            formula(false);
            shownCut = cut; cut = keep;
        }
    }
    std::vector<Place> places;
    for (auto &v : viable) places.insert(places.end(), v.begin(), v.end());
    size_t viablePlaces = places.size();
    if (places.size() > wantPlaces) { std::nth_element(places.begin(), places.begin() + wantPlaces, places.end(), [](const Place &x, const Place &y) { return x.pre > y.pre; }); places.resize(wantPlaces); }
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
                addColumn(cheese, half, 99, x, z, 0.6666666666666666, -56, 8, made, &f.cheese[at]); addColumn(layer, half, 99, x, z, 8.0, -56, 8, made, &f.layer[at]); addColumn(flood, half, 99, x, z, 0.67, -56, 8, made, &f.flood[at]);
            }
        });
    }
    double fineDone = seconds();

    // 6. the score of each of those places: its largest cluster on the fine grid
    struct Site { long gi, gj; int score, open, inner; float surface; };
    std::vector<std::vector<Site>> found(threads);
    std::atomic<size_t> nextGroup(0); std::vector<std::thread> pool;
    auto fillY = [](uint64_t reached, uint64_t open) { // every bit of open joined to a bit of reached along the column
        uint64_t up = reached, down = reached, pass = open;
        for (int k = 1; k < 64; k <<= 1) { up |= pass & (up << k); pass &= pass << k; }
        pass = open;
        for (int k = 1; k < 64; k <<= 1) { down |= pass & (down >> k); pass &= pass >> k; }
        return up | down;
    };
    for (int t = 0; t < threads; t++) pool.emplace_back([&, t] {
        std::vector<uint64_t> levelsOpen(FINE * FINE), cell(FINE * FINE), seen(FINE * FINE); std::vector<std::pair<int, uint64_t>> stack;
        for (size_t g; (g = nextGroup++) + 1 < groups.size();) {
            const Pair &p = pairs[places[groups[g]].pair]; int ia = p.a / NA, ja = p.a % NA, ib = p.b / NB, jb = p.b % NB;
            // the open-and-dry heights of every fine column, a bit per height (not the lowest: y -56 is under the lava level)
            for (int di = 0; di < WINDOW; di++) for (int dj = 0; dj < WINDOW; dj++) {
                size_t sa = (size_t) fine[0].slot[(size_t) ((ia + di) % NA) * NA + (ja + dj) % NA] * 4 * FINE_LEVELS, sb = (size_t) fine[1].slot[(size_t) ((ib + di) % NB) * NB + (jb + dj) % NB] * 4 * FINE_LEVELS;
                for (int sub = 0; sub < 4; sub++) {
                    const float *ca = &fine[0].cheese[sa + sub * FINE_LEVELS], *cb = &fine[1].cheese[sb + sub * FINE_LEVELS], *la = &fine[0].layer[sa + sub * FINE_LEVELS], *lb = &fine[1].layer[sb + sub * FINE_LEVELS],
                                *fa = &fine[0].flood[sa + sub * FINE_LEVELS], *fb = &fine[1].flood[sb + sub * FINE_LEVELS];
                    uint64_t bits = 0;
                    for (int l = 1; l < FINE_LEVELS; l++) {
                        float c = ca[l] + cb[l] + 0.27f;
                        if (c >= 0) continue;
                        if (c < -1) c = -1;
                        float ly = la[l] + lb[l];
                        if (c + 4 * ly * ly < 0 && fa[l] + fb[l] <= 0.4f) bits |= 1ULL << l;
                    }
                    levelsOpen[(size_t) (2 * di + (sub >> 1)) * FINE + 2 * dj + (sub & 1)] = bits;
                }
            }
            for (size_t at = groups[g]; at < groups[g + 1]; at++) {
                const Place &q = places[at];
                // the surface estimate of each quarter x quarter of the window, now the close one, and the heights that count there
                uint64_t under[16]; double surface = 0, x = q.gi * STEP, z = q.gj * STEP, side = WINDOW * STEP;
                for (int u = 0; u < 4; u++) for (int v = 0; v < 4; v++) {
                    double here = surfaceEstimate(&biomeNoise, (int) floor((x + side * (2 * u + 1) / 8) / 4), (int) floor((z + side * (2 * v + 1) / 8) / 4), 5, 3, 3); surface += here / 16;
                    int l = 0;
                    while (l + 1 < FINE_LEVELS && -56 + 8 * (l + 1) <= here - MARGIN) l++;
                    under[u * 4 + v] = (2ULL << l) - 1;
                }
                int points = 0;
                for (int di = 0; di < FINE; di++) for (int dj = 0; dj < FINE; dj++) { uint64_t bits = levelsOpen[di * FINE + dj] & under[(di / 8) * 4 + dj / 8]; cell[di * FINE + dj] = bits; seen[di * FINE + dj] = 0; points += __builtin_popcountll(bits); }
                int best = 0;
                if (points >= 1000) for (int start = 0; start < FINE * FINE; start++) {
                    for (uint64_t left; (left = cell[start] & ~seen[start]) != 0;) {
                        uint64_t first = fillY(left & -left, cell[start]); seen[start] |= first; int count = __builtin_popcountll(first); stack.push_back({start, first});
                        while (!stack.empty()) {
                            int i = stack.back().first; uint64_t bits = stack.back().second; stack.pop_back();
                            int dj = i % FINE, di = i / FINE;
                            auto visit = [&](int j) {
                                uint64_t fresh = bits & cell[j] & ~seen[j];
                                if (!fresh) return;
                                fresh = fillY(fresh, cell[j]) & ~seen[j]; seen[j] |= fresh; count += __builtin_popcountll(fresh); stack.push_back({j, fresh});
                            };
                            if (dj > 0) visit(i - 1); if (dj + 1 < FINE) visit(i + 1);
                            if (di > 0) visit(i - FINE); if (di + 1 < FINE) visit(i + FINE);
                        }
                        best = std::max(best, count);
                    }
                }
                found[t].push_back({q.gi, q.gj, best, p.open, points, (float) surface});
            }
        }
    });
    for (auto &t : pool) t.join();
    std::vector<Site> sites;
    for (auto &f : found) sites.insert(sites.end(), f.begin(), f.end());
    std::sort(sites.begin(), sites.end(), [](const Site &x, const Site &y) { return x.score > y.score; });
    // the best first, skipping windows that overlap one already printed by more than half
    std::vector<Site> kept;
    for (const Site &s : sites) {
        if ((int) kept.size() >= top) break;
        bool overlap = false;
        for (const Site &k : kept) if (labs(k.gi - s.gi) < WINDOW / 2 + 1 && labs(k.gj - s.gj) < WINDOW / 2 + 1) { overlap = true; break; }
        if (overlap) continue;
        kept.push_back(s);
        printf("%lld %.0f %.0f %d %d %.0f %d\n", (long long) seed, (s.gi + WINDOW / 2.0) * STEP, (s.gj + WINDOW / 2.0) * STEP, s.score, s.open, s.surface, s.inner);
    }
    fprintf(stderr, "seed %lld: tables %.1f s, %zu pairs with mean cheese <= %.3f (lowest %.3f) %.1f s, formula %.1f s, terrain at %zu places (%.1f samples each, cut %d, %zu kept) %.1f s, fine tables for %zu pairs (%zu columns, %.1f%%) %.1f s, "
            "clusters at %zu places %.1f s; best score %d\n", (long long) seed, tablesDone, pairs.size(), usedThreshold, lowestMean, pairsDone - tablesDone, formulaDone - pairsDone, allPlaces.load(), (double) samples.load() / std::max<size_t>(1, allPlaces.load()), shownCut, viablePlaces, terrainDone - formulaDone,
            groups.size() - 1, fineColumns, 100.0 * fineColumns / ((double) NA * NA + (double) NB * NB), fineDone - terrainDone, places.size(), seconds() - fineDone, kept.empty() ? 0 : kept[0].score);
}
