// icecaves/iceseeds.cpp: how much of a seed's world is under ice caves climate, in a few milliseconds a seed.
// A seed with more of it has more places for the scanner (icepairs) to look at, at the same cost for the scanner's
// tables and climate test, and gives more large caverns for the time (measured on 513 seeds: the quarter with the most
// places under ice caves had six times the caverns of 2M+ of the quarter with the least).
// POINTS random columns of the world (the same for every seed): cold = the temperature is -0.8 or less (no shift of the
// sampling position); ocean = cold and the continentalness in the ice caves range (-1.05 .. -0.19); hits (FULL=1) = the
// whole climate inside the ice caves box (also humidity up to 0.7, weirdness 0.5 .. 1.1). What differs from seed to seed:
// the cold share (-0.8 is far out in the tail of the temperature's two wide octaves: 0.55% to 2.3% of a world), and how
// the cold areas lie on the oceans (the wide octaves of the two noises repeat with periods of exactly 2 : 1, so their
// overlap is a property of the seed). The weirdness changes over a few hundred blocks and averages out over a world.
// Output per seed: "SEED ocean cold hits" (stdin: seeds, a line each; other columns are kept after the counts).
// build: g++ -O3 -march=native -std=c++17 -ffp-contract=off -I ../vendor/cubiomes iceseeds.cpp ../vendor/cub_*.o -lpthread -o iceseeds
// usage: iceseeds [THREADS=12] [POINTS=200000] [FULL=0] < seeds
extern "C" {
#include "biomenoise.h"
}
#include "fastnoise.hpp"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

static inline double stack(const DoublePerlinNoise *n, double x, double z, int from, int to) {   // octaves from .. to - 1 of both stacks
    const double f = 337.0 / 331.0; double v = 0;
    for (int i = from; i < to && i < n->octA.octcnt; i++) { const PerlinNoise *p = n->octA.octaves + i; v += p->amplitude * fastPerlin(p, x * p->lacunarity, 0, z * p->lacunarity); }
    for (int i = from; i < to && i < n->octB.octcnt; i++) { const PerlinNoise *p = n->octB.octaves + i; v += p->amplitude * fastPerlin(p, x * f * p->lacunarity, 0, z * f * p->lacunarity); }
    return v * n->amplitude;
}
static inline bool inBox(const BiomeNoise &bn, double x, double z, bool gate) {
    auto fixed = [](double v) { return (long) (10000.0F * (float) v); };
    const double wide = stack(&bn.climate[NP_TEMPERATURE], x, z, 0, 1);
    if (gate && wide > -0.8 + 0.28) return false;                       // (the narrow octave is within 0.27)
    if (fixed(wide + stack(&bn.climate[NP_TEMPERATURE], x, z, 1, 99)) > -8000) return false;
    const long w = fixed(stack(&bn.climate[NP_WEIRDNESS], x, z, 0, 99)); if (w < 5000 || w > 11000) return false;
    const long c = fixed(stack(&bn.climate[NP_CONTINENTALNESS], x, z, 0, 99)); if (c < -10500 || c > -1900) return false;
    return fixed(stack(&bn.climate[NP_HUMIDITY], x, z, 0, 99)) <= 7000;
}

int main(int argc, char **argv) {
    const int threads = argc > 1 ? atoi(argv[1]) : 12, points = argc > 2 ? atoi(argv[2]) : 200000; const bool full = argc > 3 && atoi(argv[3]);
    std::vector<std::string> lines; for (std::string l; std::getline(std::cin, l);) if (!l.empty()) lines.push_back(l);
    std::vector<int> xs(points), zs(points); uint64_t state = 0x9E3779B97F4A7C15ULL;
    auto next = [&] { state ^= state << 13; state ^= state >> 7; state ^= state << 17; return state; };
    for (int i = 0; i < points; i++) { xs[i] = (int) (next() % 14990000) - 7495000; zs[i] = (int) (next() % 14990000) - 7495000; }   // quart positions over the whole world
    std::atomic<size_t> at{0}; std::mutex lock; std::vector<std::thread> pool;
    for (int t = 0; t < threads; t++) pool.emplace_back([&] {
        BiomeNoise bn; initBiomeNoise(&bn, MC_26_3);
        for (size_t i; (i = at.fetch_add(1)) < lines.size();) {
            char *rest; const uint64_t seed = (uint64_t) strtoll(lines[i].c_str(), &rest, 10); setBiomeSeed(&bn, seed, 0);
            int cold = 0, hits = 0, ocean = 0;
            for (int k = 0; k < points; k++) {
                const double x = xs[k], z = zs[k], wide = stack(&bn.climate[NP_TEMPERATURE], x, z, 0, 1);
                if (wide > -0.8 + 0.28) continue;                       // (the narrow octave is within 0.27)
                if ((long) (10000.0F * (float) (wide + stack(&bn.climate[NP_TEMPERATURE], x, z, 1, 99))) > -8000) continue;
                cold++;
                const long c = (long) (10000.0F * (float) stack(&bn.climate[NP_CONTINENTALNESS], x, z, 0, 99)); if (c < -10500 || c > -1900) continue;
                ocean++;
                if (full && inBox(bn, x, z, false)) hits++;
            }
            std::lock_guard<std::mutex> hold(lock); printf("%lld %d %d %d%s\n", (long long) seed, ocean, cold, hits, rest);
        }
    });
    for (auto &t : pool) t.join();
}
