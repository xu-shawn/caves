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
//      cave noise but different terrain: the surface estimate there (cubiomes' depth) decides how many heights count
//      and the score of a place is its largest cluster of open-and-dry table points that touch (6 neighbours): the
//      biggest caverns are the ones that hang together, not the windows with the most air
// Prints the best windows: "seed x z cluster open surface points" (x, z = window centre; cluster = table points in the
// largest cluster at least 40 blocks under the surface, of 256 columns x 16 heights over 1037 x 1037 blocks, 67,230
// blocks a point; open = all open-and-dry points without the surface limit; points = those under the surface).
//
// build: g++ -O3 -march=native -std=c++17 -I vendor/cubiomes cavepairs.cpp vendor/cub_*.o -lpthread -o cavepairs
// usage: cavepairs SEED [PAIRS=600000] [TOP=300] [THREADS=8]     (STATS=1: only "seed lowestMean threshold open1 open100 open1000")
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

static const int NA = 1011, NB = 993, LEVELS = 16, WINDOW = 16, MEAN_LEVELS = 10, MARGIN = 40;
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
    const uint64_t seed = strtoull(argv[1], 0, 10); const size_t wantPairs = (argc > 2 ? strtoull(argv[2], 0, 10) : 600000) / 2; const int top = argc > 3 ? atoi(argv[3]) : 300, threads = argc > 4 ? atoi(argv[4]) : 8;
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
    size_t withTerrain = std::min(pairs.size(), (size_t) 40000);
    double formulaDone = seconds();
    if (getenv("STATS")) { // what the tables alone say about the seed: lowest window means, and the best open counts of its pairs
        printf("%lld %.4f %.4f %d %d %d\n", (long long) seed, lowestMean, usedThreshold, pairs[byOpen[0]].open, pairs[byOpen[std::min(pairs.size() - 1, (size_t) 99)]].open,
               pairs[byOpen[std::min(pairs.size() - 1, (size_t) 999)]].open);
        return 0;
    }

    // 4. where the pairs are in the world, and the terrain there
    BiomeNoise biomeNoise; initBiomeNoise(&biomeNoise, MC_26_3); setBiomeSeed(&biomeNoise, seed, 0);
    struct Site { long gi, gj; int score, open, inner; float surface; };
    std::vector<std::vector<Site>> found(threads);
    std::atomic<size_t> nextPair(0); std::vector<std::thread> pool;
    for (int t = 0; t < threads; t++) pool.emplace_back([&, t] {
        std::vector<uint16_t> levelsOpen(WINDOW * WINDOW); std::vector<uint8_t> cell(WINDOW * WINDOW * LEVELS); std::vector<int> stack;
        for (size_t index; (index = nextPair++) < withTerrain;) {
            const Pair &p = pairs[byOpen[index]]; int ia = p.a / NA, ja = p.a % NA, ib = p.b / NB, jb = p.b % NB;
            // the open-and-dry heights of every column, a bit per height
            for (int di = 0; di < WINDOW; di++) for (int dj = 0; dj < WINDOW; dj++) {
                int xa = (ia + di) % NA, za = (ja + dj) % NA, xb = (ib + di) % NB, zb = (jb + dj) % NB;
                const float *ca = table[0].at(table[0].cheese, xa, za), *cb = table[1].at(table[1].cheese, xb, zb), *la = table[0].at(table[0].layer, xa, za), *lb = table[1].at(table[1].layer, xb, zb),
                            *fa = table[0].at(table[0].flood, xa, za), *fb = table[1].at(table[1].flood, xb, zb);
                uint16_t bits = 0;
                for (int k = 0; k < LEVELS; k++) {
                    float c = ca[k] + cb[k] + 0.27f;
                    if (c >= 0) continue;
                    if (c < -1) c = -1;
                    float l = la[k] + lb[k];
                    if (c + 4 * l * l < 0 && fa[k] + fb[k] <= 0.4f) bits |= (uint16_t) (1u << k);
                }
                levelsOpen[di * WINDOW + dj] = bits;
            }
            // g = ia + 1011 k with 18 k = ib - ia (mod 993): k = ((ib - ia) / 3) * 276 (mod 331), 276 being 1 / 6 mod 331
            auto solve = [](int a, int b) { int d = (((b - a) / 3) % 331 + 331) % 331; return (long) a + 1011L * ((d * 276) % 331); };
            long gi0 = solve(ia, ib), gj0 = solve(ja, jb);
            for (long gi = gi0 - 2L * COMMON; gi <= gi0 + COMMON; gi += COMMON) for (long gj = gj0 - 2L * COMMON; gj <= gj0 + COMMON; gj += COMMON) {
                double x = gi * STEP, z = gj * STEP, side = WINDOW * STEP;
                if (x < -BORDER || z < -BORDER || x + side > BORDER || z + side > BORDER) continue;
                // the surface estimate of each quarter x quarter of the window, and the highest height that counts there
                int highest[4][4]; double surface = 0;
                for (int u = 0; u < 4; u++) for (int v = 0; v < 4; v++) {
                    int64_t np[6];
                    sampleBiomeNoise(&biomeNoise, np, (int) floor((x + side * (2 * u + 1) / 8) / 4), 0, (int) floor((z + side * (2 * v + 1) / 8) / 4), NULL, SAMPLE_NO_BIOME);
                    double here = 128.0 * np[4] / 10000.0; surface += here / 16;
                    int k = -1;
                    while (k + 1 < LEVELS && levelY(k + 1) <= here - MARGIN) k++;
                    highest[u][v] = k;
                }
                int points = 0;
                for (int di = 0; di < WINDOW; di++) for (int dj = 0; dj < WINDOW; dj++) {
                    uint16_t bits = levelsOpen[di * WINDOW + dj]; int top = highest[di / 4][dj / 4];
                    for (int k = 0; k < LEVELS; k++) { bool on = k <= top && ((bits >> k) & 1); cell[(di * WINDOW + dj) * LEVELS + k] = on; points += on; }
                }
                int best = 0;
                for (int start = 0; start < WINDOW * WINDOW * LEVELS; start++) {
                    if (cell[start] != 1) continue;
                    int count = 0; cell[start] = 2; stack.push_back(start);
                    while (!stack.empty()) {
                        int i = stack.back(); stack.pop_back(); count++;
                        int k = i % LEVELS, dj = (i / LEVELS) % WINDOW, di = i / (LEVELS * WINDOW);
                        auto visit = [&](int j) { if (cell[j] == 1) { cell[j] = 2; stack.push_back(j); } };
                        if (k > 0) visit(i - 1); if (k + 1 < LEVELS) visit(i + 1);
                        if (dj > 0) visit(i - LEVELS); if (dj + 1 < WINDOW) visit(i + LEVELS);
                        if (di > 0) visit(i - LEVELS * WINDOW); if (di + 1 < WINDOW) visit(i + LEVELS * WINDOW);
                    }
                    best = std::max(best, count);
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
    fprintf(stderr, "seed %lld: tables %.1f s, %zu pairs with mean cheese <= %.3f (lowest %.3f) %.1f s, formula %.1f s, terrain for %zu pairs at %zu places %.1f s; best score %d\n", (long long) seed,
            tablesDone, pairs.size(), usedThreshold, lowestMean, pairsDone - tablesDone, formulaDone - pairsDone, withTerrain, sites.size(), seconds() - formulaDone, kept.empty() ? 0 : kept[0].score);
}
