// icecaves/icephase.cpp: what a seed's ice caves are worth, from the y offsets of its noise layers.
// A Perlin layer is weakest on its lattice planes and strongest half way between them, and where a layer's planes
// fall in y is one random number of the seed (the cave search's gate: ../docs/PROGRESSION.md). The climate noises
// are sampled at y = 0 only, so a layer's offset decides how far that layer swings anywhere in the world:
//   temperature (octaves -10 and -8): ice caves need -0.8 or less, far in the tail: the cold share of a world
//   ridge = weirdness (-7, -6, -5): ice caves need 0.5 or more: how large the lobes over 0.5 are
//   continentalness (-9 ..): the oceans
//   cave_cheese (-8, -7, -6; cells 384, 192, 96 blocks tall): at the heights of the ice caves (y -53 .. 24)
// stats N [THREADS] [FIRST=0]: for N seeds (a mix of the index), every layer's phase and what the world holds:
//   "SEED  phases...  cold ocean hits  w wwide wverywide"
//   cold: of 200,000 random columns those at temperature <= -0.8; ocean: of those, continentalness in -1.05 .. -0.19;
//   hits: of those, humidity <= 0.7 and weirdness in 0.5 .. 1.1 (the ice caves box); w: of 100,000 other columns those
//   with weirdness >= 0.5; wwide: with the four columns 128 blocks away too; wverywide: and those 256 away.
// build: g++ -O3 -march=native -std=c++17 -ffp-contract=off -I .. -I ../vendor/cubiomes icephase.cpp ../vendor/cub_*.o -lpthread -o icephase
extern "C" {
#include "biomenoise.h"
#include "rng.h"
}
#include "name_hashes.h"
#include "fastnoise.hpp"
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct NoiseLayerName { const char *noise, *octave; double at; };   // at: the reference height in cells of the layer (0 for the climate, sampled at y = 0)
static const double ICE_Y = -16;                           // the middle of the ice caves' heights
static const NoiseLayerName LAYERS[] = {
    {"minecraft:temperature", "octave_-10", 0}, {"minecraft:temperature", "octave_-8", 0},
    {"minecraft:ridge", "octave_-7", 0}, {"minecraft:ridge", "octave_-6", 0}, {"minecraft:ridge", "octave_-5", 0},
    {"minecraft:continentalness", "octave_-9", 0}, {"minecraft:continentalness", "octave_-8", 0}, {"minecraft:continentalness", "octave_-7", 0}, {"minecraft:continentalness", "octave_-6", 0},
    {"minecraft:vegetation", "octave_-8", 0}, {"minecraft:vegetation", "octave_-7", 0},
    {"minecraft:cave_cheese", "octave_-8", ICE_Y * (2.0 / 3) / 256}, {"minecraft:cave_cheese", "octave_-7", ICE_Y * (2.0 / 3) / 128}, {"minecraft:cave_cheese", "octave_-6", ICE_Y * (2.0 / 3) / 64},
    {"minecraft:cave_layer", "octave_-8", ICE_Y * 8.0 / 256}, {"minecraft:aquifer_fluid_level_floodedness", "octave_-7", ICE_Y * 0.67 / 128},   // (the layer noise: cells 32 blocks tall)
    // more layers, for the fits of what else matters (their y scales as the noise router samples them)
    {"minecraft:erosion", "octave_-9", 0}, {"minecraft:erosion", "octave_-8", 0},
    {"minecraft:pillar", "octave_-7", ICE_Y * 0.3 / 128}, {"minecraft:pillar", "octave_-6", ICE_Y * 0.3 / 64},
    {"minecraft:pillar_rareness", "octave_-8", ICE_Y / 256}, {"minecraft:pillar_thickness", "octave_-8", ICE_Y / 256},
    {"minecraft:cave_entrance", "octave_-7", ICE_Y * 0.5 / 128}, {"minecraft:cave_entrance", "octave_-6", ICE_Y * 0.5 / 64}, {"minecraft:cave_entrance", "octave_-5", ICE_Y * 0.5 / 32},
    {"minecraft:spaghetti_roughness", "octave_-5", ICE_Y / 32}, {"minecraft:aquifer_fluid_level_spread", "octave_-5", ICE_Y * 0.7142857142857143 / 32},
    {"minecraft:aquifer_barrier", "octave_-3", ICE_Y * 0.5 / 8}, {"minecraft:noodle", "octave_-8", ICE_Y / 256}};
static const int NLAYERS = sizeof LAYERS / sizeof LAYERS[0];

// the phases of every layer, stack A then stack B: phase[2 * layer + half] in 0 .. 1 (0.5 = half way between two lattice planes)
static inline void phasesOf(uint64_t seed, double *phase) {
    Xoroshiro world; xSetSeed(&world, seed); const uint64_t worldLo = xNextLong(&world), worldHi = xNextLong(&world);
    for (int l = 0; l < NLAYERS;) {
        const NameHash &noise = nameHash(LAYERS[l].noise); Xoroshiro random = {worldLo ^ noise.lo, worldHi ^ noise.hi};
        int last = l; while (last + 1 < NLAYERS && !strcmp(LAYERS[last + 1].noise, LAYERS[l].noise)) last++;
        for (int half = 0; half < 2; half++) {
            const uint64_t forkLo = xNextLong(&random), forkHi = xNextLong(&random);
            for (int k = l; k <= last; k++) {
                const NameHash &o = nameHash(LAYERS[k].octave); Xoroshiro r = {forkLo ^ o.lo, forkHi ^ o.hi};
                xNextDouble(&r); double v = xNextDouble(&r) * 256.0 + LAYERS[k].at * (half ? 337.0 / 331.0 : 1.0);
                phase[2 * k + half] = v - std::floor(v);
            }
        }
        l = last + 1;
    }
}

static inline double stack(const DoublePerlinNoise *n, double x, double z, int from, int to) {
    const double f = 337.0 / 331.0; double v = 0;
    for (int i = from; i < to && i < n->octA.octcnt; i++) { const PerlinNoise *p = n->octA.octaves + i; v += p->amplitude * fastPerlin(p, x * p->lacunarity, 0, z * p->lacunarity); }
    for (int i = from; i < to && i < n->octB.octcnt; i++) { const PerlinNoise *p = n->octB.octaves + i; v += p->amplitude * fastPerlin(p, x * f * p->lacunarity, 0, z * f * p->lacunarity); }
    return v * n->amplitude;
}
static inline uint64_t mixIndex(uint64_t i) { uint64_t z = i * 0x9E3779B97F4A7C15ULL; z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL; z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL; return z ^ (z >> 31); }

// scan: seeds whose climate layers sit half way between their lattice planes (temperature octave -10, weirdness octaves
// -7 and -6, both stacks, each within its window of 0.5), and, with CHEESE windows, whose two lowest cheese layers are
// where the cave noise is lowest at the ice caves' heights (0.48 and 0.455 at y -16). Stops at the first layer outside
// its window: most seeds cost the world's two mixes and one layer.
static void scan(uint64_t first, uint64_t count, int threads, double wT, double w7, double w6, double c8, double c7) {
    const NameHash &temperature = nameHash("minecraft:temperature"), &ridge = nameHash("minecraft:ridge"), &cheese = nameHash("minecraft:cave_cheese");
    const NameHash &o10 = nameHash("octave_-10"), &o8 = nameHash("octave_-8"), &o7 = nameHash("octave_-7"), &o6 = nameHash("octave_-6");
    std::atomic<uint64_t> next{0}, found{0}; std::mutex lock; std::vector<std::thread> pool; const uint64_t BATCH = 1 << 16;
    for (int t = 0; t < threads; t++) pool.emplace_back([&] {
        std::string out; char buf[160];
        for (uint64_t from; (from = next.fetch_add(BATCH)) < count;) {
            for (uint64_t i = from; i < from + BATCH && i < count; i++) {
                const uint64_t seed = mixIndex(first + i);
                Xoroshiro world; xSetSeed(&world, seed); const uint64_t worldLo = xNextLong(&world), worldHi = xNextLong(&world);
                auto phase = [](uint64_t forkLo, uint64_t forkHi, const NameHash &o, double at) { Xoroshiro r = {forkLo ^ o.lo, forkHi ^ o.hi}; xNextDouble(&r); const double v = xNextDouble(&r) * 256.0 + at; return v - std::floor(v); };
                double p[10];
                Xoroshiro random = {worldLo ^ temperature.lo, worldHi ^ temperature.hi};
                uint64_t lo = xNextLong(&random), hi = xNextLong(&random); p[0] = phase(lo, hi, o10, 0); if (std::fabs(p[0] - 0.5) > wT) continue;
                lo = xNextLong(&random); hi = xNextLong(&random); p[1] = phase(lo, hi, o10, 0); if (std::fabs(p[1] - 0.5) > wT) continue;
                random = {worldLo ^ ridge.lo, worldHi ^ ridge.hi};
                lo = xNextLong(&random); hi = xNextLong(&random); p[2] = phase(lo, hi, o7, 0); if (std::fabs(p[2] - 0.5) > w7) continue;
                p[4] = phase(lo, hi, o6, 0); if (std::fabs(p[4] - 0.5) > w6) continue;
                lo = xNextLong(&random); hi = xNextLong(&random); p[3] = phase(lo, hi, o7, 0); if (std::fabs(p[3] - 0.5) > w7) continue;
                p[5] = phase(lo, hi, o6, 0); if (std::fabs(p[5] - 0.5) > w6) continue;
                p[6] = p[7] = p[8] = p[9] = -1;
                if (c8 < 0.5) {
                    random = {worldLo ^ cheese.lo, worldHi ^ cheese.hi}; const double f = 337.0 / 331.0;
                    lo = xNextLong(&random); hi = xNextLong(&random); p[6] = phase(lo, hi, o8, ICE_Y * (2.0 / 3) / 256); if (std::fabs(p[6] - 0.48) > c8) continue;
                    p[8] = phase(lo, hi, o7, ICE_Y * (2.0 / 3) / 128); if (std::fabs(p[8] - 0.455) > c7) continue;
                    lo = xNextLong(&random); hi = xNextLong(&random); p[7] = phase(lo, hi, o8, ICE_Y * (2.0 / 3) / 256 * f); if (std::fabs(p[7] - 0.48) > c8) continue;
                    p[9] = phase(lo, hi, o7, ICE_Y * (2.0 / 3) / 128 * f); if (std::fabs(p[9] - 0.455) > c7) continue;
                }
                snprintf(buf, sizeof buf, "%lld %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f\n", (long long) seed, p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9]); out += buf; found++;
            }
            if (!out.empty()) { std::lock_guard<std::mutex> hold(lock); fputs(out.c_str(), stdout); fflush(stdout); out.clear(); }
        }
    });
    for (auto &t : pool) t.join();
    fprintf(stderr, "%llu seeds scanned, %llu kept\n", (unsigned long long) count, (unsigned long long) found.load());
}

// gate: the general scan. SPEC is a list of layers "noise,octave,at,centre,halfwidth" separated by ';' (noise and octave as in
// the game's names without "minecraft:" and "octave_"; at: the reference height in cells of the layer, as in LAYERS; centre:
// the phase wanted there; a layer passes when its phase is within halfwidth of the centre, the short way round). Layers of
// one noise must be listed together; a seed is dropped at the first layer of either stack outside its window, so the
// tightest noise goes first. Prints "SEED phases..." (stack A then B per layer, in SPEC's order).
struct GateLayer { NameHash octave; double at, centre, half; };
struct GateNoise { NameHash noise; std::vector<GateLayer> layers; };
static void gate(uint64_t first, uint64_t count, int threads, const char *spec) {
    std::vector<GateNoise> noises; std::string text(spec); size_t from = 0; std::string lastName; double kept = 1; std::vector<std::string> keep;
    while (from < text.size()) {
        size_t to = text.find(';', from); if (to == std::string::npos) to = text.size();
        char name[64], octave[16]; double at, centre, half;
        if (sscanf(text.substr(from, to - from).c_str(), " %63[^,],%15[^,],%lf,%lf,%lf", name, octave, &at, &centre, &half) == 5) {
            if (noises.empty() || lastName != name) { keep.push_back(std::string("minecraft:") + name); noises.push_back({nameHash(keep.back().c_str()), {}}); lastName = name; }
            keep.push_back(std::string("octave_") + octave); noises.back().layers.push_back({nameHash(keep.back().c_str()), at, centre, half}); kept *= 4 * half * half;
        }
        from = to + 1;
    }
    fprintf(stderr, "gate of %zu noises: %.3g of the seeds pass\n", noises.size(), kept);
    std::atomic<uint64_t> next{0}, found{0}; std::mutex lock; std::vector<std::thread> pool; const uint64_t BATCH = 1 << 16;
    for (int t = 0; t < threads; t++) pool.emplace_back([&] {
        std::string out; char buf[32]; double p[64];
        for (uint64_t start; (start = next.fetch_add(BATCH)) < count;) {
            for (uint64_t i = start; i < start + BATCH && i < count; i++) {
                const uint64_t seed = mixIndex(first + i);
                Xoroshiro world; xSetSeed(&world, seed); const uint64_t worldLo = xNextLong(&world), worldHi = xNextLong(&world);
                bool pass = true; int at = 0;
                for (size_t n = 0; n < noises.size() && pass; n++) {
                    Xoroshiro random = {worldLo ^ noises[n].noise.lo, worldHi ^ noises[n].noise.hi};
                    for (int half = 0; half < 2 && pass; half++) {
                        const uint64_t forkLo = xNextLong(&random), forkHi = xNextLong(&random);
                        for (size_t k = 0; k < noises[n].layers.size(); k++) {
                            const GateLayer &g = noises[n].layers[k]; Xoroshiro r = {forkLo ^ g.octave.lo, forkHi ^ g.octave.hi};
                            xNextDouble(&r); double v = xNextDouble(&r) * 256.0 + g.at * (half ? 337.0 / 331.0 : 1.0); v -= std::floor(v);
                            double d = std::fabs(v - g.centre); if (d > 0.5) d = 1 - d;
                            if (d > g.half) { pass = false; break; }
                            p[at + 2 * k + half] = v;
                        }
                    }
                    at += 2 * (int) noises[n].layers.size();
                }
                if (!pass) continue;
                out += std::to_string((long long) seed); for (int k = 0; k < at; k++) { snprintf(buf, sizeof buf, " %.3f", p[k]); out += buf; } out += "\n"; found++;
            }
            if (!out.empty()) { std::lock_guard<std::mutex> hold(lock); fputs(out.c_str(), stdout); fflush(stdout); out.clear(); }
        }
    });
    for (auto &t : pool) t.join();
    fprintf(stderr, "%llu seeds scanned, %llu kept\n", (unsigned long long) count, (unsigned long long) found.load());
}

int main(int argc, char **argv) {
    if (argc >= 6 && !strcmp(argv[1], "gate")) { gate(strtoull(argv[2], 0, 10), strtoull(argv[3], 0, 10), atoi(argv[4]), argv[5]); return 0; }   // gate FIRST COUNT THREADS SPEC
    if (argc >= 4 && !strcmp(argv[1], "scan")) {   // scan FIRST COUNT [THREADS=1] [WT=0.045 W7=0.027 W6=0.036] [C8=1 C7=1]: "SEED T10A T10B W7A W7B W6A W6B ch8A ch8B ch7A ch7B"
        scan(strtoull(argv[2], 0, 10), strtoull(argv[3], 0, 10), argc > 4 ? atoi(argv[4]) : 1, argc > 5 ? atof(argv[5]) : 0.045, argc > 6 ? atof(argv[6]) : 0.027, argc > 7 ? atof(argv[7]) : 0.036, argc > 8 ? atof(argv[8]) : 1, argc > 9 ? atof(argv[9]) : 1);
        return 0;
    }
    if (argc >= 2 && !strcmp(argv[1], "phases")) {   // seeds on stdin -> "SEED phases..."
        char line[256]; double phase[2 * NLAYERS];
        while (fgets(line, sizeof line, stdin)) { const long long seed = strtoll(line, 0, 10); phasesOf((uint64_t) seed, phase); printf("%lld", seed); for (int k = 0; k < 2 * NLAYERS; k++) printf(" %.4f", phase[k]); printf("\n"); }
        return 0;
    }
    if (argc < 3 || strcmp(argv[1], "stats")) { fprintf(stderr, "usage: icephase stats N [THREADS=8] [FIRST=0]   (or seeds on stdin with N = 0);  icephase phases < seeds\n"); return 1; }
    const long n = atol(argv[2]); const int threads = argc > 3 ? atoi(argv[3]) : 8; const uint64_t first = argc > 4 ? strtoull(argv[4], 0, 10) : 0;
    std::vector<uint64_t> seeds;
    if (n > 0) for (long i = 0; i < n; i++) seeds.push_back(mixIndex(first + i));
    else { char line[256]; while (fgets(line, sizeof line, stdin)) seeds.push_back((uint64_t) strtoll(line, 0, 10)); }
    const long lensPoints = getenv("LENS") ? atol(getenv("LENS")) : 0;
    const int POINTS = 200000, WPOINTS = 100000; std::vector<int> xs(POINTS + WPOINTS), zs(POINTS + WPOINTS); uint64_t state = 0x9E3779B97F4A7C15ULL;
    auto next = [&] { state ^= state << 13; state ^= state >> 7; state ^= state << 17; return state; };
    for (size_t i = 0; i < xs.size(); i++) { xs[i] = (int) (next() % 14990000) - 7495000; zs[i] = (int) (next() % 14990000) - 7495000; }
    std::atomic<size_t> at{0}; std::mutex lock; std::vector<std::thread> pool; std::atomic<long> mismatches{0};
    for (int t = 0; t < threads; t++) pool.emplace_back([&] {
        BiomeNoise bn; initBiomeNoise(&bn, MC_26_3);
        for (size_t i; (i = at.fetch_add(1)) < seeds.size();) {
            const uint64_t seed = seeds[i]; double phase[2 * NLAYERS]; phasesOf(seed, phase); setBiomeSeed(&bn, seed, 0);
            // (check of the offsets against cubiomes: the temperature's and the weirdness' first layers)
            if (std::fabs(phase[0] - bn.climate[NP_TEMPERATURE].octA.octaves[0].d2) > 1e-9 || std::fabs(phase[1] - bn.climate[NP_TEMPERATURE].octB.octaves[0].d2) > 1e-9 ||
                std::fabs(phase[4] - bn.climate[NP_WEIRDNESS].octA.octaves[0].d2) > 1e-9 || std::fabs(phase[7] - bn.climate[NP_WEIRDNESS].octB.octaves[1].d2) > 1e-9) mismatches++;
            auto fixed = [](double v) { return (long) (10000.0F * (float) v); };
            int cold = 0, ocean = 0, hits = 0, w = 0, wWide = 0, wVery = 0;
            for (int k = 0; k < POINTS; k++) {
                const double x = xs[k], z = zs[k], wide = stack(&bn.climate[NP_TEMPERATURE], x, z, 0, 1);
                if (wide > -0.8 + 0.28) continue;
                if (fixed(wide + stack(&bn.climate[NP_TEMPERATURE], x, z, 1, 99)) > -8000) continue;
                cold++;
                const long c = fixed(stack(&bn.climate[NP_CONTINENTALNESS], x, z, 0, 99)); if (c < -10500 || c > -1900) continue;
                ocean++;
                const long ww = fixed(stack(&bn.climate[NP_WEIRDNESS], x, z, 0, 99)); if (ww < 5000 || ww > 11000) continue;
                if (fixed(stack(&bn.climate[NP_HUMIDITY], x, z, 0, 99)) <= 7000) hits++;
            }
            auto over = [&](double x, double z) { return fixed(stack(&bn.climate[NP_WEIRDNESS], x, z, 0, 99)) >= 5000; };
            for (int k = POINTS; k < POINTS + WPOINTS; k++) {
                const double x = xs[k], z = zs[k];
                if (!over(x, z)) continue;
                w++;
                if (!(over(x + 32, z) && over(x - 32, z) && over(x, z + 32) && over(x, z - 32))) continue;
                wWide++;
                if (over(x + 64, z) && over(x - 64, z) && over(x, z + 64) && over(x, z - 64)) wVery++;
            }
            // lenses: of LENS random columns, those inside the ice caves box whose four neighbours 128 blocks away are inside too (lens5), and those 256 away (lens9)
            int lens1 = 0, lens5 = 0, lens9 = 0;
            auto inBox = [&](double x, double z) {
                if (fixed(stack(&bn.climate[NP_TEMPERATURE], x, z, 0, 99)) > -8000) return false;
                const long ww = fixed(stack(&bn.climate[NP_WEIRDNESS], x, z, 0, 99)); if (ww < 5000 || ww > 11000) return false;
                const long c = fixed(stack(&bn.climate[NP_CONTINENTALNESS], x, z, 0, 99)); if (c < -10500 || c > -1900) return false;
                return fixed(stack(&bn.climate[NP_HUMIDITY], x, z, 0, 99)) <= 7000; };
            uint64_t st = 0x2545F4914F6CDD1DULL;
            for (long k = 0; k < lensPoints; k++) {
                st ^= st << 13; st ^= st >> 7; st ^= st << 17; const double x = (double) (long) (st % 14990000) - 7495000; st ^= st << 13; st ^= st >> 7; st ^= st << 17; const double z = (double) (long) (st % 14990000) - 7495000;
                if (stack(&bn.climate[NP_TEMPERATURE], x, z, 0, 1) > -0.8 + 0.28) continue;
                if (!inBox(x, z)) continue;
                lens1++;
                if (!(inBox(x + 32, z) && inBox(x - 32, z) && inBox(x, z + 32) && inBox(x, z - 32))) continue;
                lens5++;
                if (inBox(x + 64, z) && inBox(x - 64, z) && inBox(x, z + 64) && inBox(x, z - 64)) lens9++;
            }
            std::string out = std::to_string((long long) seed);
            char buf[64]; for (int k = 0; k < 2 * NLAYERS; k++) { snprintf(buf, sizeof buf, " %.4f", phase[k]); out += buf; }
            snprintf(buf, sizeof buf, "  %d %d %d  %d %d %d  %d %d %d", cold, ocean, hits, w, wWide, wVery, lens1, lens5, lens9); out += buf;
            std::lock_guard<std::mutex> hold(lock); puts(out.c_str());
        }
    });
    for (auto &t : pool) t.join();
    if (mismatches) fprintf(stderr, "%ld seeds whose offsets differ from cubiomes'\n", mismatches.load());
}
