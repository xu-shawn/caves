// Checks of the GPU parts on the box:
//   gpucheck terrain [BATCHES=8]   the scan with and without the terrain score in the kernel: the kernel's survivors must
//                                  hold every seed the exact CPU score passes; and the speed of both
//   gpucheck look [SEEDS=20000]    the GPU's first coarse look against cpuprelook.hpp's low8y for seeds past phases and
//                                  terrain; and its speed
// build: g++ -O3 -march=native -std=c++17 -I . -I cubiomes stats/gpucheck.cpp gpuscan.o gpulook.o cub_noise.o -L/usr/local/cuda/lib64 -lcudart -lpthread -o gpucheck
#include "cpuprelook.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>
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
    const std::string mode = argc > 1 ? argv[1] : "terrain"; const double c8 = 0.542, w8 = 0.045, c7 = 0.584, w7 = 0.10, terrain = 0.45;
    static const double AT[4] = {16.0 / 256, 16.0 / 128, 16.0 / 256 * 1.0181268882175227, 16.0 / 128 * 1.0181268882175227};
    const NameHash cheeseHash = nameHash("minecraft:cave_cheese"), o8 = nameHash("octave_-8"), o7 = nameHash("octave_-7");
    GpuScanWindows w = {cheeseHash.lo, cheeseHash.hi, o8.lo, o8.hi, o7.lo, o7.hi, {0, 0, 0, 0}, {0, 0, 0, 0}};
    for (int i = 0; i < 4; i++) { double centre = i % 2 ? c7 : c8, width = i % 2 ? w7 : w8, lo = centre - width - AT[i], hi = centre + width - AT[i]; w.low[i] = (int64_t) (lo * 35184372088832.0) - (1 << 20); w.high[i] = (int64_t) (hi * 35184372088832.0) + (1 << 20); }
    const char *noises[3] = {"minecraft:erosion", "minecraft:continentalness", "minecraft:ridge"}, *octaves[5] = {"octave_-9", "octave_-8", "octave_-7", "octave_-6", "octave_-5"};
    for (int i = 0; i < 3; i++) { NameHash h = nameHash(noises[i]); w.noiseLo[i] = h.lo; w.noiseHi[i] = h.hi; }
    for (int i = 0; i < 5; i++) { NameHash h = nameHash(octaves[i]); w.octLo[i] = h.lo; w.octHi[i] = h.hi; }
    w.terrainCut = (float) (-terrain + 5e-4);
    const uint32_t THREADS = 1 << 20, CAPACITY = 1 << 22; const int PER = 4096; const uint64_t BATCH = (uint64_t) THREADS * PER, base0 = 5000000000000000ULL;
    auto now = [] { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); };
    std::vector<uint64_t> found(CAPACITY);
    if (mode == "terrain") {
        const int batches = argc > 2 ? atoi(argv[2]) : 8; std::vector<uint64_t> exact, kernel; double seconds[2];
        for (int pass = 0; pass < 2; pass++) {
            w.terrain = pass; if (gpuscan_init(&w, CAPACITY)) return 1;
            gpuscan_batch(base0 - BATCH, THREADS, PER, found.data()); double t0 = now(); uint64_t base = base0;
            for (int b = 0; b < batches; b++) { long n = gpuscan_batch(base, THREADS, PER, found.data()); if (n < 0 || (uint64_t) n > CAPACITY) { printf("batch failed or overflowed: %ld\n", n); return 1; } (pass ? kernel : exact).insert((pass ? kernel : exact).end(), found.begin(), found.begin() + n); base += BATCH; }
            seconds[pass] = now() - t0;
        }
        size_t phasesOnly = exact.size(); std::vector<uint64_t> pass; for (uint64_t s : exact) if (terrainScore(s) <= -terrain) pass.push_back(s);
        std::sort(pass.begin(), pass.end()); std::sort(kernel.begin(), kernel.end()); size_t missing = 0; for (uint64_t s : pass) missing += !std::binary_search(kernel.begin(), kernel.end(), s);
        printf("%d batches of %.3g seeds: without the terrain score %.3g seeds/s (%zu survivors), with it %.3g seeds/s (%zu survivors); the exact score passes %zu of the first, %zu of them missing from the kernel's\n", batches, (double) BATCH, batches * (double) BATCH / seconds[0], phasesOnly, batches * (double) BATCH / seconds[1], kernel.size(), pass.size(), missing);
        return 0;
    }
    const int want = argc > 2 ? atoi(argv[2]) : 20000; w.terrain = 1; if (gpuscan_init(&w, CAPACITY) || gpulook_init(want)) return 1;
    std::vector<uint64_t> seeds; uint64_t base = base0;
    while ((int) seeds.size() < want) { long n = gpuscan_batch(base, THREADS, PER, found.data()); if (n < 0) return 1; for (long i = 0; i < n && (int) seeds.size() < want; i++) if (terrainScore(found[i]) <= -terrain) seeds.push_back(found[i]); base += BATCH; }
    std::vector<LookSeed> tables(want); std::vector<float> gpu(want), cpu(want); std::unique_ptr<CoarseLook> look(new CoarseLook);
    double a = now(); for (int i = 0; i < want; i++) lookSeed(seeds[i], tables[i]); double b = now();
    gpulook_batch(tables.data(), std::min(want, 1024), gpu.data()); double c = now(); for (int rep = 0; rep < 5; rep++) if (gpulook_batch(tables.data(), want, gpu.data())) return 1; double d = now();
    double worst = 0, sq = 0, t0 = now(); for (int i = 0; i < want; i++) { TableSeed ts; tableSeed(seeds[i], ts); cpu[i] = look->low8y(ts); double e = gpu[i] - cpu[i]; sq += e * e; worst = std::max(worst, std::fabs(e)); } double t1 = now();
    int both = 0, onlyGpu = 0, onlyCpu = 0; for (int i = 0; i < want; i++) { bool g = gpu[i] <= -0.1748f, k = cpu[i] <= -0.1748f; both += g && k; onlyGpu += g && !k; onlyCpu += k && !g; }
    printf("%d seeds: GPU coarse look %.0f seeds/s (%.2f us a seed); against the CPU's number: rms difference %.5f, largest %.5f; past the cut -0.1748: both %d, GPU only %d, CPU only %d; host: lookSeed %.2f us a seed, CPU look with its tables %.1f us a seed\n",
           want, 5.0 * want / (d - c), (d - c) / (5.0 * want) * 1e6, std::sqrt(sq / want), worst, both, onlyGpu, onlyCpu, (b - a) / want * 1e6, (t1 - t0) / want * 1e6);
}
