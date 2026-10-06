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
//    above their top caverns 6 blocks higher. A seed passes if the weighted sum of cos(2 pi phase) over the 12 lowest
//    terrain layers (weights below; -1 = mid-cell) is at most -TERRAIN.
// 2. The table gate: what the offsets do not say is in the permutation tables ("table luck": worth a factor 1.7 in
//    big-cavern sites per standard deviation). How low does cave_cheese get, averaged over a 512-block window and the
//    heights -56 .. 72, for its A stack and its B stack? The gate value is the summed window mean (A + B) at the pair
//    count that matches cavepairs' cut. It needs the two lowest octaves only, and no noise samples: analyticgate.hpp
//    computes every window mean exactly from the gradients (14 ms a seed; the sampled version in seedgate3_sampled.cpp
//    took 90 ms with a coarse look in front that lost one seed in seven).
// Prints "seed gate lowest 0 phase8A phase8B phase7A phase7B terrainScore" for the seeds that pass 1 and whose
// gate + terrainScore / 92 (the two in the same units: 92 per unit of log yield) is at most PRINT.
//
// build: g++ -O3 -march=native -std=c++17 -I vendor/cubiomes seedgate3.cpp vendor/cub_noise.o -lpthread -o seedgate3
// usage: seedgate3 FIRST LAST [THREADS=8] [PRINT=-0.318] [C8=0.55 W8=0.03 C7=0.595 W7=0.06] [TERRAIN=0.55]       (PRINT 9: every seed that passes 1; W 1 and TERRAIN -9: no phase gate)
#include "analyticgate.hpp"
#include <immintrin.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
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
// The first of those four checks for 4 seeds at once (AVX2): the bits of the seeds from seed whose octave -8 A phase is
// within the window. Same steps as phases() up to the first layer: xSetSeed, the world pair, the fork, the layer source,
// its second double. The phase of y = 24 is frac(double x 256 + 1/16), and the double is (n >> 11) / 2^53, so the
// fraction is the low 45 bits of n >> 11: an integer window (low .. high, a little wider than the exact one; phases()
// decides after it).
static inline __m256i mul64(__m256i a, uint64_t c) { // the low 64 bits of a x c
    const __m256i cl = _mm256_set1_epi64x((int64_t) (c & 0xffffffffULL)), ch = _mm256_set1_epi64x((int64_t) (c >> 32));
    __m256i cross = _mm256_add_epi64(_mm256_mul_epu32(_mm256_srli_epi64(a, 32), cl), _mm256_mul_epu32(a, ch));
    return _mm256_add_epi64(_mm256_mul_epu32(a, cl), _mm256_slli_epi64(cross, 32));
}
static inline __m256i rotl4(__m256i v, int k) { return _mm256_or_si256(_mm256_slli_epi64(v, k), _mm256_srli_epi64(v, 64 - k)); }
struct Xoroshiro4 { __m256i lo, hi; };
static inline __m256i next4(Xoroshiro4 &x) {
    __m256i l = x.lo, h = x.hi, n = _mm256_add_epi64(rotl4(_mm256_add_epi64(l, h), 17), l);
    h = _mm256_xor_si256(h, l); x.lo = _mm256_xor_si256(_mm256_xor_si256(rotl4(l, 49), h), _mm256_slli_epi64(h, 21)); x.hi = rotl4(h, 28);
    return n;
}
static inline int firstPhase4(uint64_t seed, const NameHash &cheese, const NameHash &o8, int64_t low, int64_t high) {
    auto mix = [](__m256i v, int shift) { return _mm256_xor_si256(v, _mm256_srli_epi64(v, shift)); };
    auto with = [](__m256i v, uint64_t c) { return _mm256_xor_si256(v, _mm256_set1_epi64x((int64_t) c)); };
    __m256i l = with(_mm256_add_epi64(_mm256_set1_epi64x((int64_t) seed), _mm256_set_epi64x(3, 2, 1, 0)), 0x6a09e667f3bcc909ULL), h = _mm256_add_epi64(l, _mm256_set1_epi64x((int64_t) 0x9e3779b97f4a7c15ULL));
    l = mul64(mix(l, 30), 0xbf58476d1ce4e5b9ULL); h = mul64(mix(h, 30), 0xbf58476d1ce4e5b9ULL); l = mul64(mix(l, 27), 0x94d049bb133111ebULL); h = mul64(mix(h, 27), 0x94d049bb133111ebULL);
    Xoroshiro4 world = {mix(l, 31), mix(h, 31)}; __m256i worldLo = next4(world), worldHi = next4(world);
    Xoroshiro4 random = {with(worldLo, cheese.lo), with(worldHi, cheese.hi)}; __m256i forkLo = next4(random), forkHi = next4(random);
    Xoroshiro4 layer = {with(forkLo, o8.lo), with(forkHi, o8.hi)}; next4(layer);
    __m256i v = _mm256_and_si256(_mm256_srli_epi64(next4(layer), 11), _mm256_set1_epi64x((1LL << 45) - 1));
    __m256i in = _mm256_and_si256(_mm256_cmpgt_epi64(v, _mm256_set1_epi64x(low)), _mm256_cmpgt_epi64(_mm256_set1_epi64x(high), v));
    return _mm256_movemask_pd(_mm256_castsi256_pd(in));
}

// sum of weight x cos(2 pi phase at y = 0) over the lowest terrain layers, stacks A and B each: erosion octaves -9, -8,
// continentalness octave -9, ridges octaves -7, -6, -5 (the offsets as cubiomes' setBiomeSeed draws them). The weights
// are the layers' measured effects on the scanner's cut over 2,463 seeds scanned in full (every optimum is mid-cell),
// in units of 92 x the gate value.
static inline double terrainScore(uint64_t seed) {
    static const NameHash noises[3] = {nameHash("minecraft:erosion"), nameHash("minecraft:continentalness"), nameHash("minecraft:ridge")};
    static const NameHash o9 = nameHash("octave_-9"), o8 = nameHash("octave_-8"), o7 = nameHash("octave_-7"), o6 = nameHash("octave_-6"), o5 = nameHash("octave_-5");
    static const double TWO_PI = 6.283185307179586;
    Xoroshiro world; xSetSeed(&world, seed);
    uint64_t worldLo = xNextLong(&world), worldHi = xNextLong(&world); double score = 0;
    for (int n = 0; n < 3; n++) {
        Xoroshiro random = {worldLo ^ noises[n].lo, worldHi ^ noises[n].hi};
        for (int half = 0; half < 2; half++) {
            uint64_t forkLo = xNextLong(&random), forkHi = xNextLong(&random);
            auto offset = [&](const NameHash &h) { Xoroshiro r = {forkLo ^ h.lo, forkHi ^ h.hi}; xNextDouble(&r); return xNextDouble(&r) * 256.0; };
            if (n == 0) score += 0.21 * std::cos(TWO_PI * offset(o9)) + 0.065 * std::cos(TWO_PI * offset(o8));
            else if (n == 1) score += 0.10 * std::cos(TWO_PI * offset(o9));
            else score += 0.09 * std::cos(TWO_PI * offset(o7)) + 0.125 * std::cos(TWO_PI * offset(o6)) + 0.05 * std::cos(TWO_PI * offset(o5));
        }
    }
    return score;
}
int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: seedgate3 FIRST LAST [THREADS=8] [PRINT=-0.318] [C8=0.55 W8=0.03 C7=0.595 W7=0.06] [TERRAIN=0.55]\n"); return 1; }
    uint64_t first = strtoull(argv[1], 0, 10), last = strtoull(argv[2], 0, 10);
    int threads = argc > 3 ? atoi(argv[3]) : 8; double print = argc > 4 ? atof(argv[4]) : -0.318;
    const double c8 = argc > 5 ? atof(argv[5]) : 0.55, w8 = argc > 6 ? atof(argv[6]) : 0.03, c7 = argc > 7 ? atof(argv[7]) : 0.595, w7 = argc > 8 ? atof(argv[8]) : 0.06, terrain = argc > 9 ? atof(argv[9]) : 0.55;
    const NameHash cheeseHash = nameHash("minecraft:cave_cheese"), o8 = nameHash("octave_-8"), o7 = nameHash("octave_-7");
    const uint64_t CHUNK = 1 << 16; std::atomic<uint64_t> next(first); std::mutex lock; std::vector<std::thread> pool;
    for (int t = 0; t < threads; t++) pool.emplace_back([&] {
        AnalyticGate gate;
        // the integer window of the first check (a little wide), if it does not wrap; else every seed takes the long way
        const double lowPhase = c8 - w8 - 16.0 / 256, highPhase = c8 + w8 - 16.0 / 256; const bool quick = lowPhase > 1e-6 && highPhase < 1 - 1e-6;
        const int64_t low = (int64_t) (lowPhase * 35184372088832.0) - (1 << 20), high = (int64_t) (highPhase * 35184372088832.0) + (1 << 20);
        for (uint64_t from; (from = next.fetch_add(CHUNK)) <= last && from >= first;) {
            for (uint64_t four = from; four < from + CHUNK && four <= last; four += 4) {
                for (int mask = quick ? firstPhase4(four, cheeseHash, o8, low, high) : 15; mask; mask &= mask - 1) {
                    uint64_t seed = four + __builtin_ctz(mask); double p[4];
                    if (seed > last || !phases(seed, cheeseHash, o8, o7, c8, w8, c7, w7, p)) continue;
                    double land = terrainScore(seed);
                    if (land > -terrain) continue;
                    NormalNoise cheese = makeNoise(seed, "minecraft:cave_cheese");
                    float lowest, value = gate.value(cheese, lowest);
                    if (value + land / 92 > print) continue;
                    std::lock_guard<std::mutex> guard(lock);
                    printf("%lld %.4f %.4f 0 %.3f %.3f %.3f %.3f %.3f\n", (long long) seed, value, lowest, p[0], p[2], p[1], p[3], land); fflush(stdout);
                }
            }
        }
    });
    for (auto &t : pool) t.join();
}
