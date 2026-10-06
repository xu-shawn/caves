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
//   4. the cheese noise has as much in its 64-block octave as in the 256 and 128 ones, and the 64.8-block grid cannot see
//      it; so for the best pairs (by open count) the tables are made again where their windows are, at half the step
//      (32.4 blocks) and 33 heights (y -56 + 8 l): only a few percent of the table area, and now the formula on them is
//      close to what stage 2 computes on its 16-block grid
//   5. each pair is at 2 or 3 places per axis in the world (the Chinese remainder theorem gives them) with the same
//      cave noise but different terrain: the surface estimate there (cubiomes' depth) decides how many heights count
//      and the score of a place is its largest cluster of open-and-dry fine points that touch (6 neighbours): the
//      biggest caverns are the ones that hang together, not the windows with the most air
// Prints the best windows: "seed x z cluster open surface points" (x, z = window centre; cluster = fine points in the
// largest cluster at least MARGIN blocks under the surface, of 32 x 32 columns x 32 heights over 1037 x 1037 blocks, 8,404
// blocks a point; open = the pair's open-and-dry points on the coarse grid, no surface limit; points = fine ones under
// the surface).
//
// build: g++ -O3 -march=native -std=c++17 -I vendor/cubiomes cavepairs.cpp vendor/cub_*.o -lpthread -o cavepairs
// usage: cavepairs SEED [PAIRS=600000] [TOP=300] [THREADS=8] [TERRAIN_PAIRS=40000]     (STATS=1: only "seed lowestMean threshold open1 open100 open1000")
#include "cavenoise.hpp"
extern "C" {
#include "biomenoise.h"
}
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <unordered_set>
#include <vector>

static const int NA = 1011, NB = 993, LEVELS = 16, WINDOW = 16, MEAN_LEVELS = 10, MARGIN = 48;
static const int FINE_LEVELS = 33, FINE = 2 * WINDOW;   // the fine grid: half steps, y = -56 + 8 l
static const int COMMON = 334641;                 // lcm(1011, 993): the cave noise repeats every that many steps
static const double STEP = 65536.0 / NA;
static const double BORDER = 29990000.0;
static inline int levelY(int level) { return -40 + 16 * level; }

struct Tables {
    int n;
    std::vector<float> cheese, layer, flood; // [i][j][level]
    float *at(std::vector<float> &t, int i, int j) { return t.data() + ((size_t) i * n + j) * LEVELS; }
    const float *at(const std::vector<float> &t, int i, int j) const { return t.data() + ((size_t) i * n + j) * LEVELS; }
};

// the sum of every second layer of a noise: half 0 is the A stack, half 1 the B stack
static inline float sampleHalf(const NormalNoise &noise, int half, double x, double y, double z) {
    float value = 0;
    for (size_t i = half; i < noise.layers.size(); i += 2) {
        const NoiseLayer &layer = noise.layers[i];
        value += layer.amplitude * (float) samplePerlin(&layer.perlin, x * layer.frequency, y * layer.frequency, z * layer.frequency, 0, 0);
    }
    return value;
}

template <class Body> static void parallel(int threads, int n, Body body) {
    std::atomic<int> next(0); std::vector<std::thread> pool;
    for (int t = 0; t < threads; t++) pool.emplace_back([&] { for (int i; (i = next++) < n;) body(i); });
    for (auto &t : pool) t.join();
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: cavepairs SEED [PAIRS=300000] [TOP=300] [THREADS=8]\n"); return 1; }
    const uint64_t seed = strtoull(argv[1], 0, 10); const size_t wantPairs = (argc > 2 ? strtoull(argv[2], 0, 10) : 600000) / 2; const int top = argc > 3 ? atoi(argv[3]) : 300, threads = argc > 4 ? atoi(argv[4]) : 8; const size_t terrainPairs = argc > 5 ? strtoull(argv[5], 0, 10) : 40000;
    auto clock0 = std::chrono::steady_clock::now();
    auto seconds = [&] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - clock0).count(); };
    const NormalNoise cheese = makeNoise(seed, "minecraft:cave_cheese"), layer = makeNoise(seed, "minecraft:cave_layer"), flood = makeNoise(seed, "minecraft:aquifer_fluid_level_floodedness");

    // 1. tables
    Tables table[2];
    for (int half = 0; half < 2; half++) {
        Tables &t = table[half]; t.n = half ? NB : NA;
        t.cheese.resize((size_t) t.n * t.n * LEVELS); t.layer.resize(t.cheese.size()); t.flood.resize(t.cheese.size());
        parallel(threads, t.n, [&](int i) {
            for (int j = 0; j < t.n; j++) {
                double x = i * STEP, z = j * STEP;
                float *c = t.at(t.cheese, i, j), *l = t.at(t.layer, i, j), *f = t.at(t.flood, i, j);
                for (int k = 0; k < LEVELS; k++) {
                    int y = levelY(k);
                    c[k] = sampleHalf(cheese, half, x, y * 0.6666666666666666, z);
                    l[k] = sampleHalf(layer, half, x, y * 8.0, z);
                    f[k] = sampleHalf(flood, half, x, y * 0.67, z);
                }
            }
        });
    }
    double tablesDone = seconds();

    // 2. the mean cheese of every window (periodic), per stack; a pair is the window origins (A index, B index)
    struct Pair { int a, b; float meanCheese; int counts[LEVELS], inner[LEVELS]; int open; };
    std::vector<Pair> pairs; float lowestMean = 0, usedThreshold = 0;
    for (int select : {8, 16}) {
    std::vector<float> mean[2];
    for (int half = 0; half < 2; half++) {
        const Tables &t = table[half]; int n = t.n;
        std::vector<double> column((size_t) n * n);
        for (int i = 0; i < n; i++) for (int j = 0; j < n; j++) { const float *c = t.at(t.cheese, i, j); double s = 0; for (int k = 0; k < MEAN_LEVELS; k++) s += c[k]; column[(size_t) i * n + j] = s; }
        std::vector<double> rows((size_t) n * n);
        for (int i = 0; i < n; i++) for (int j = 0; j < n; j++) { double s = 0; for (int d = 0; d < select; d++) s += column[(size_t) i * n + (j + d) % n]; rows[(size_t) i * n + j] = s; }
        mean[half].resize((size_t) n * n);
        for (int i = 0; i < n; i++) for (int j = 0; j < n; j++) { double s = 0; for (int d = 0; d < select; d++) s += rows[(size_t) ((i + d) % n) * n + j]; mean[half][(size_t) i * n + j] = (float) (s / (select * select * MEAN_LEVELS)); }
    }
    // sorted: every A window, and the B windows by their remainders mod 3
    std::vector<int> orderA((size_t) NA * NA);
    for (size_t i = 0; i < orderA.size(); i++) orderA[i] = (int) i;
    std::sort(orderA.begin(), orderA.end(), [&](int a, int b) { return mean[0][a] < mean[0][b]; });
    std::vector<int> classB[9];
    for (int i = 0; i < NB; i++) for (int j = 0; j < NB; j++) classB[(i % 3) * 3 + j % 3].push_back(i * NB + j);
    std::vector<float> valuesB[9];
    for (int c = 0; c < 9; c++) {
        std::sort(classB[c].begin(), classB[c].end(), [&](int a, int b) { return mean[1][a] < mean[1][b]; });
        for (int b : classB[c]) valuesB[c].push_back(mean[1][b]);
    }
    auto classOfA = [&](int a) { return ((a / NA) % 3) * 3 + (a % NA) % 3; };
    // the threshold on the sum that gives about wantPairs pairs
    auto countPairs = [&](float threshold) {
        size_t total = 0;
        for (int a : orderA) {
            const std::vector<float> &values = valuesB[classOfA(a)];
            float limit = threshold - mean[0][a];
            if (limit < values[0]) break; // the A windows only get worse from here
            total += std::upper_bound(values.begin(), values.end(), limit) - values.begin();
            if (total > 4 * wantPairs) break;
        }
        return total;
    };
    float lowest = mean[0][orderA[0]] + std::min({valuesB[0][0], valuesB[1][0], valuesB[2][0], valuesB[3][0], valuesB[4][0], valuesB[5][0], valuesB[6][0], valuesB[7][0], valuesB[8][0]});
    float lo = lowest, hi = 0;
    for (int it = 0; it < 40; it++) { float mid = 0.5f * (lo + hi); (countPairs(mid) >= wantPairs ? hi : lo) = mid; }
    const float threshold = hi;
    for (int a : orderA) {
        int c = classOfA(a); float limit = threshold - mean[0][a];
        if (limit < valuesB[c][0]) break;
        size_t count = std::upper_bound(valuesB[c].begin(), valuesB[c].end(), limit) - valuesB[c].begin();
        for (size_t k = 0; k < count; k++) {
            int b = classB[c][k], shift = (WINDOW - select) / 2; // the origin of the 16 x 16 window with the same centre
            Pair p; p.a = ((a / NA - shift + NA) % NA) * NA + (a % NA - shift + NA) % NA; p.b = ((b / NB - shift + NB) % NB) * NB + (b % NB - shift + NB) % NB; p.meanCheese = mean[0][a] + valuesB[c][k]; pairs.push_back(p);
        }
    }
    if (select == 8) { lowestMean = lowest; usedThreshold = threshold; }
    }
    double pairsDone = seconds();

    // 3. the formula from the tables, per height
    parallel(threads, (int) pairs.size(), [&](int index) {
        Pair &p = pairs[index]; int ia = p.a / NA, ja = p.a % NA, ib = p.b / NB, jb = p.b % NB;
        for (int k = 0; k < LEVELS; k++) p.counts[k] = p.inner[k] = 0;
        for (int di = 0; di < WINDOW; di++) for (int dj = 0; dj < WINDOW; dj++) {
            int xa = (ia + di) % NA, za = (ja + dj) % NA, xb = (ib + di) % NB, zb = (jb + dj) % NB;
            const float *ca = table[0].at(table[0].cheese, xa, za), *cb = table[1].at(table[1].cheese, xb, zb), *la = table[0].at(table[0].layer, xa, za), *lb = table[1].at(table[1].layer, xb, zb),
                        *fa = table[0].at(table[0].flood, xa, za), *fb = table[1].at(table[1].flood, xb, zb);
            for (int k = 0; k < LEVELS; k++) {
                float c = ca[k] + cb[k] + 0.27f;
                if (c >= 0) continue;
                if (c < -1) c = -1;
                float l = la[k] + lb[k];
                if (c + 4 * l * l < 0 && fa[k] + fb[k] <= 0.4f) { p.counts[k]++; p.inner[k] += di >= 4 && di < 12 && dj >= 4 && dj < 12; }
            }
        }
        p.open = 0;
        for (int k = 0; k < LEVELS; k++) p.open += p.counts[k];
    });
    // the pairs worth the terrain: the best by what they could score at most
    std::vector<int> byOpen(pairs.size());
    for (size_t i = 0; i < byOpen.size(); i++) byOpen[i] = (int) i;
    std::sort(byOpen.begin(), byOpen.end(), [&](int x, int y) { return pairs[x].open > pairs[y].open; });
    size_t withTerrain = std::min(pairs.size(), terrainPairs);
    double formulaDone = seconds();
    if (getenv("STATS")) { // what the tables alone say about the seed: lowest window means, and the best open counts of its pairs
        printf("%lld %.4f %.4f %d %d %d\n", (long long) seed, lowestMean, usedThreshold, pairs[byOpen[0]].open, pairs[byOpen[std::min(pairs.size() - 1, (size_t) 99)]].open,
               pairs[byOpen[std::min(pairs.size() - 1, (size_t) 999)]].open);
        return 0;
    }

    // 4. the fine tables, where the windows of the pairs worth the terrain are: values [slot][2 x 2 half steps][height]
    struct FineTable { std::vector<int> slot; std::vector<float> cheese, layer, flood; } fine[2];
    size_t fineColumns = 0;
    for (int half = 0; half < 2; half++) {
        FineTable &f = fine[half]; int n = table[half].n; f.slot.assign((size_t) n * n, -1);
        for (size_t r = 0; r < withTerrain; r++) {
            const Pair &p = pairs[byOpen[r]]; int origin = half ? p.b : p.a, i0 = origin / n, j0 = origin % n;
            for (int di = 0; di < WINDOW; di++) for (int dj = 0; dj < WINDOW; dj++) f.slot[(size_t) ((i0 + di) % n) * n + (j0 + dj) % n] = 0;
        }
        std::vector<int> columns;
        for (int c = 0; c < n * n; c++) if (f.slot[c] == 0) { f.slot[c] = (int) columns.size(); columns.push_back(c); }
        fineColumns += columns.size();
        f.cheese.resize(columns.size() * 4 * FINE_LEVELS); f.layer.resize(f.cheese.size()); f.flood.resize(f.cheese.size());
        parallel(threads, (int) columns.size(), [&](int s) {
            int i = columns[s] / n, j = columns[s] % n;
            for (int sub = 0; sub < 4; sub++) {
                double x = (i + 0.5 * (sub >> 1)) * STEP, z = (j + 0.5 * (sub & 1)) * STEP; size_t at = ((size_t) s * 4 + sub) * FINE_LEVELS;
                for (int l = 0; l < FINE_LEVELS; l++) {
                    int y = -56 + 8 * l;
                    f.cheese[at + l] = sampleHalf(cheese, half, x, y * 0.6666666666666666, z); f.layer[at + l] = sampleHalf(layer, half, x, y * 8.0, z); f.flood[at + l] = sampleHalf(flood, half, x, y * 0.67, z);
                }
            }
        });
    }
    double fineDone = seconds();

    // 5. where the pairs are in the world, and the terrain there
    BiomeNoise biomeNoise; initBiomeNoise(&biomeNoise, MC_26_3); setBiomeSeed(&biomeNoise, seed, 0);
    struct Site { long gi, gj; int score, open, inner; float surface; };
    std::vector<std::vector<Site>> found(threads);
    std::atomic<size_t> nextPair(0); std::vector<std::thread> pool;
    auto fillY = [](uint64_t reached, uint64_t open) { // every bit of open joined to a bit of reached along the column
        uint64_t up = reached, down = reached, pass = open;
        for (int k = 1; k < 64; k <<= 1) { up |= pass & (up << k); pass &= pass << k; }
        pass = open;
        for (int k = 1; k < 64; k <<= 1) { down |= pass & (down >> k); pass &= pass >> k; }
        return up | down;
    };
    for (int t = 0; t < threads; t++) pool.emplace_back([&, t] {
        std::vector<uint64_t> levelsOpen(FINE * FINE), cell(FINE * FINE), seen(FINE * FINE); std::vector<std::pair<int, uint64_t>> stack;
        for (size_t index; (index = nextPair++) < withTerrain;) {
            const Pair &p = pairs[byOpen[index]]; int ia = p.a / NA, ja = p.a % NA, ib = p.b / NB, jb = p.b % NB;
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
            // g = ia + 1011 k with 18 k = ib - ia (mod 993): k = ((ib - ia) / 3) * 276 (mod 331), 276 being 1 / 6 mod 331
            auto solve = [](int a, int b) { int d = (((b - a) / 3) % 331 + 331) % 331; return (long) a + 1011L * ((d * 276) % 331); };
            long gi0 = solve(ia, ib), gj0 = solve(ja, jb);
            for (long gi = gi0 - 2L * COMMON; gi <= gi0 + COMMON; gi += COMMON) for (long gj = gj0 - 2L * COMMON; gj <= gj0 + COMMON; gj += COMMON) {
                double x = gi * STEP, z = gj * STEP, side = WINDOW * STEP;
                if (x < -BORDER || z < -BORDER || x + side > BORDER || z + side > BORDER) continue;
                // the surface estimate of each quarter x quarter of the window, and the heights that count there
                uint64_t under[4][4]; double surface = 0;
                for (int u = 0; u < 4; u++) for (int v = 0; v < 4; v++) {
                    int64_t np[6];
                    sampleBiomeNoise(&biomeNoise, np, (int) floor((x + side * (2 * u + 1) / 8) / 4), 0, (int) floor((z + side * (2 * v + 1) / 8) / 4), NULL, SAMPLE_NO_BIOME);
                    double here = 128.0 * np[4] / 10000.0; surface += here / 16;
                    int l = 0;
                    while (l + 1 < FINE_LEVELS && -56 + 8 * (l + 1) <= here - MARGIN) l++;
                    under[u][v] = (2ULL << l) - 1;
                }
                int points = 0;
                for (int di = 0; di < FINE; di++) for (int dj = 0; dj < FINE; dj++) { uint64_t bits = levelsOpen[di * FINE + dj] & under[di / 8][dj / 8]; cell[di * FINE + dj] = bits; seen[di * FINE + dj] = 0; points += __builtin_popcountll(bits); }
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
                found[t].push_back({gi, gj, best, p.open, points, (float) surface});
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
    fprintf(stderr, "seed %lld: tables %.1f s, %zu pairs with mean cheese <= %.3f (lowest %.3f) %.1f s, formula %.1f s, fine tables for %zu columns (%.1f%%) %.1f s, terrain for %zu pairs at %zu places %.1f s; best score %d\n",
            (long long) seed, tablesDone, pairs.size(), usedThreshold, lowestMean, pairsDone - tablesDone, formulaDone - pairsDone, fineColumns, 100.0 * fineColumns / ((double) NA * NA + (double) NB * NB), fineDone - formulaDone,
            withTerrain, sites.size(), seconds() - fineDone, kept.empty() ? 0 : kept[0].score);
}
