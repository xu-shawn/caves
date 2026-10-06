// The seed-level filters of the cave search, cheapest first.
//
// 1. The phase gate (7 ns a seed). The cave_cheese noise is a sum of Perlin layers, and a Perlin layer is weakest on its
//    lattice planes and strongest half way between them. The two lowest octaves have cells 384 and 192 blocks tall, so
//    where their lattice planes fall in y decides how hard the noise can swing at the heights caves live at - and that is
//    one number per layer, its y offset, the second double out of the layer's random source. No noise table is needed: 14
//    xoroshiro steps give the four offsets (octaves -8 and -7, stacks A and B). Over 620,000 seeds they predict the table
//    gate below with r = 0.986, and over 700 seeds scanned in full, the number of big-cavern sites of a seed with r 0.84.
//    A seed passes if the phase of y = 24 (0 and 1 = lattice planes) of each of the four layers is within W8 / W7 of C8 / C7.
//    The terrain has the same handle. Continentalness, erosion and ridges are 3D Perlin layers read at y = 0, so a layer's
//    y offset says how far that slice is from a lattice plane, and with it how strongly the terrain of the whole seed
//    varies. Big caverns need tall terrain above them: over 830 seeds searched in full, the seeds whose lowest erosion
//    and continentalness layers are read mid-cell have their biggest formula cavern 0.9M blocks bigger and the terrain
//    above their top caverns 6 blocks higher. A seed passes if the weighted sum of cos(2 pi phase) over the 8 lowest
//    terrain layers (weights below; -1 = mid-cell) is at most -TERRAIN.
// 2. The table gate: how low does cave_cheese get, averaged over a 518-block window, for its A stack and its B stack? The
//    gate value is the summed window mean (A + B) at the pair count that matches cavepairs' cut; it only needs the two
//    lowest octaves on a coarse grid: one period of each stack, 506 x 506 columns, 5 heights. In front of it the same
//    number on a grid twice as coarse (253 x 253 columns, 3 heights, 2 x 2 windows; a sixth of the work): only seeds whose
//    coarse value is at most PRE go on.
// Prints "seed gate lowest coarse phase8A phase8B phase7A phase7B terrainScore" for the seeds that pass 1 and the coarse look.
//
// build: g++ -O3 -march=native -std=c++17 -I vendor/cubiomes seedgate3.cpp vendor/cub_noise.o -lpthread -o seedgate3
// usage: seedgate3 FIRST LAST [THREADS=8] [PRE=-0.4116] [C8=0.55 W8=0.02 C7=0.595 W7=0.05] [TERRAIN=0.46]       (PRE 9: every seed that passes 1; W 1 and TERRAIN -9: no phase gate)
#include "cavenoise.hpp"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
struct Gate {
    int n, window, levels, levelStep, share; size_t pairs; // pairs: the share of all pairs that cavepairs keeps (300,000 of 1011^2 x 993^2 / 9), for this grid
    std::vector<float> column, rows, mean[2];
    Gate(int n, int window, int levels, int levelStep, size_t pairs, int share) : n(n), window(window), levels(levels), levelStep(levelStep), share(share), pairs(pairs), column((size_t) n * n), rows((size_t) n * n) { mean[0].resize((size_t) n * n); mean[1].resize((size_t) n * n); }
    // the window-mean sum at the pair count, and the lowest sum
    float value(const NormalNoise &cheese, float &lowest) {
        for (int half = 0; half < 2; half++) {
            double period = half ? 65536.0 * 331 / 337 : 65536.0, step = period / n;
            for (int i = 0; i < n; i++) for (int j = 0; j < n; j++) {
                double x = i * step, z = j * step; float sum = 0;
                for (int level = 0; level < levels; level++) {
                    double y = (-40 + levelStep * level) * 0.6666666666666666;
                    for (int o = 0; o < 2; o++) { const NoiseLayer &l = cheese.layers[2 * o + half]; sum += l.amplitude * (float) fastPerlin(&l.perlin, x * l.frequency, y * l.frequency, z * l.frequency); }
                }
                column[(size_t) i * n + j] = sum;
            }
            for (int i = 0; i < n; i++) for (int j = 0; j < n; j++) { float s = 0; for (int d = 0; d < window; d++) s += column[(size_t) i * n + (j + d) % n]; rows[(size_t) i * n + j] = s; }
            for (int i = 0; i < n; i++) for (int j = 0; j < n; j++) { float s = 0; for (int d = 0; d < window; d++) s += rows[(size_t) ((i + d) % n) * n + j]; mean[half][(size_t) i * n + j] = s / (window * window * levels); }
            // the coarse look only needs the low end: the lowest eighth, sorted (the count of pairs is then a little short)
            size_t keep = mean[half].size() / share;
            if (share > 1) std::nth_element(mean[half].begin(), mean[half].begin() + keep, mean[half].end());
            std::sort(mean[half].begin(), mean[half].begin() + keep);
        }
        size_t keep = mean[0].size() / share;
        auto pairsBelow = [&](float threshold) { // both lists rise, so the count of B values under the limit only falls along A
            size_t total = 0, under = keep;
            for (size_t i = 0; i < keep; i++) {
                float limit = threshold - mean[0][i];
                while (under > 0 && mean[1][under - 1] > limit) under--;
                if (under == 0) break;
                total += under;
                if (total > 4 * pairs) break;
            }
            return total;
        };
        lowest = mean[0][0] + mean[1][0]; float lo = lowest, hi = 0;
        for (int it = 0; it < 30; it++) { float mid = 0.5f * (lo + hi); (pairsBelow(mid) >= pairs ? hi : lo) = mid; }
        return hi;
    }
};
// the phase of y = 24 in the cells of the four lowest cheese layers (octave -8 A, -7 A, -8 B, -7 B in phase[0..3]), from the
// seed alone; stops at the first layer outside its window (most seeds: after 6 of the 14 random steps) and returns false
static inline bool phases(uint64_t seed, const NameHash &cheese, const NameHash &o8, const NameHash &o7, double c8, double w8, double c7, double w7, double *phase) {
    static const double AT[4] = {16.0 / 256, 16.0 / 128, 16.0 / 256 * 1.0181268882175227, 16.0 / 128 * 1.0181268882175227}; // 24 x 2/3 in cells
    Xoroshiro world; xSetSeed(&world, seed);
    uint64_t worldLo = xNextLong(&world), worldHi = xNextLong(&world);
    Xoroshiro random = {worldLo ^ cheese.lo, worldHi ^ cheese.hi};
    for (int half = 0; half < 2; half++) {
        uint64_t forkLo = xNextLong(&random), forkHi = xNextLong(&random);
        for (int octave = 0; octave < 2; octave++) {
            const NameHash &h = octave ? o7 : o8; Xoroshiro r = {forkLo ^ h.lo, forkHi ^ h.hi};
            xNextDouble(&r); double v = xNextDouble(&r) * 256.0 + AT[half * 2 + octave];
            v -= std::floor(v); phase[half * 2 + octave] = v;
            if (std::fabs(v - (octave ? c7 : c8)) > (octave ? w7 : w8)) return false;
        }
    }
    return true;
}
// sum of weight x cos(2 pi phase at y = 0) over the lowest terrain layers: erosion octave -9, continentalness octave -9,
// ridges octaves -7 and -6, stacks A and B each (the offsets as cubiomes' setBiomeSeed draws them)
static inline double terrainScore(uint64_t seed) {
    static const NameHash noises[3] = {nameHash("minecraft:erosion"), nameHash("minecraft:continentalness"), nameHash("minecraft:ridge")};
    static const NameHash o9 = nameHash("octave_-9"), o7 = nameHash("octave_-7"), o6 = nameHash("octave_-6");
    static const double TWO_PI = 6.283185307179586;
    Xoroshiro world; xSetSeed(&world, seed);
    uint64_t worldLo = xNextLong(&world), worldHi = xNextLong(&world); double score = 0;
    for (int n = 0; n < 3; n++) {
        Xoroshiro random = {worldLo ^ noises[n].lo, worldHi ^ noises[n].hi};
        for (int half = 0; half < 2; half++) {
            uint64_t forkLo = xNextLong(&random), forkHi = xNextLong(&random);
            auto offset = [&](const NameHash &h) { Xoroshiro r = {forkLo ^ h.lo, forkHi ^ h.hi}; xNextDouble(&r); return xNextDouble(&r) * 256.0; };
            if (n == 0) score += 0.20 * std::cos(TWO_PI * offset(o9));
            else if (n == 1) score += 0.10 * std::cos(TWO_PI * offset(o9));
            else score += 0.08 * (std::cos(TWO_PI * offset(o7)) + std::cos(TWO_PI * offset(o6)));
        }
    }
    return score;
}
int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: seedgate3 FIRST LAST [THREADS=8] [PRE=-0.4116] [C8=0.55 W8=0.02 C7=0.595 W7=0.05] [TERRAIN=0.46]\n"); return 1; }
    uint64_t first = strtoull(argv[1], 0, 10), last = strtoull(argv[2], 0, 10);
    int threads = argc > 3 ? atoi(argv[3]) : 8; float pre = argc > 4 ? atof(argv[4]) : -0.4116f;
    const double c8 = argc > 5 ? atof(argv[5]) : 0.55, w8 = argc > 6 ? atof(argv[6]) : 0.02, c7 = argc > 7 ? atof(argv[7]) : 0.595, w7 = argc > 8 ? atof(argv[8]) : 0.05, terrain = argc > 9 ? atof(argv[9]) : 0.46;
    const NameHash cheeseHash = nameHash("minecraft:cave_cheese"), o8 = nameHash("octave_-8"), o7 = nameHash("octave_-7");
    const uint64_t CHUNK = 1 << 16; std::atomic<uint64_t> next(first); std::mutex lock; std::vector<std::thread> pool;
    for (int t = 0; t < threads; t++) pool.emplace_back([&] {
        Gate coarse(253, 2, 3, 64, 10625, 8), full(506, 4, 5, 32, 170000, 1);
        for (uint64_t from; (from = next.fetch_add(CHUNK)) <= last && from >= first;) {
            for (uint64_t seed = from; seed < from + CHUNK && seed <= last; seed++) {
                double p[4];
                if (!phases(seed, cheeseHash, o8, o7, c8, w8, c7, w7, p)) continue;
                double land = terrainScore(seed);
                if (land > -terrain) continue;
                NormalNoise cheese = makeNoise(seed, "minecraft:cave_cheese");
                float lowest, coarseValue = coarse.value(cheese, lowest);
                if (coarseValue > pre) continue;
                float value = full.value(cheese, lowest);
                std::lock_guard<std::mutex> guard(lock);
                printf("%lld %.4f %.4f %.4f %.3f %.3f %.3f %.3f %.3f\n", (long long) seed, value, lowest, coarseValue, p[0], p[2], p[1], p[3], land); fflush(stdout);
            }
        }
    });
    for (auto &t : pool) t.join();
}
