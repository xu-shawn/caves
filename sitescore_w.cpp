// Stage-1 features of given sites, for fitting the stage-1 score against the real-terrain air of stage 2.
// For a 256-block window around each site: 16 x 16 columns, heights y -48 .. 200 step 8; a point is open if the cheese
// formula says so, dry if the floodedness noise is at most 0.4; the surface height estimate of a column is where the
// game's depth is 0 (128 x cubiomes' depth at y = 0: offset spline of continentalness, erosion, weirdness).
// Output per site: "seed x z | surface mean, min, max | for the margins 0, 16, 32, 48, 64: open-and-dry and open points at
// least that far below the surface estimate | open-and-dry points of every height (32 numbers)"
// build: g++ -O3 -march=native -std=c++17 -I vendor/cubiomes sitescore.cpp vendor/cub_*.o -lpthread -o sitescore
// usage: sitescore THREADS < "seed x z ..."
#include "cavenoise.hpp"
extern "C" {
#include "biomenoise.h"
}
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
int main(int argc, char **argv) {
    int threads = argc > 1 ? atoi(argv[1]) : 8; const int NW = argc > 2 ? atoi(argv[2]) / 16 : 16;
    struct Site { uint64_t seed; long x, z; }; std::vector<Site> sites; std::string l;
    while (std::getline(std::cin, l)) { Site s; long long seed; if (sscanf(l.c_str(), "%lld %ld %ld", &seed, &s.x, &s.z) == 3) { s.seed = (uint64_t) seed; sites.push_back(s); } }
    std::vector<std::string> out(sites.size()); std::atomic<size_t> next(0); std::vector<std::thread> pool;
    for (int t = 0; t < threads; t++) pool.emplace_back([&] {
        BiomeNoise bn; initBiomeNoise(&bn, MC_26_3); uint64_t cached = 0; bool have = false; NormalNoise cheese, layer, flood;
        for (size_t k; (k = next++) < sites.size();) {
            const Site &s = sites[k];
            if (!have || s.seed != cached) { setBiomeSeed(&bn, s.seed, 0); cheese = makeNoise(s.seed, "minecraft:cave_cheese"); layer = makeNoise(s.seed, "minecraft:cave_layer"); flood = makeNoise(s.seed, "minecraft:aquifer_fluid_level_floodedness"); cached = s.seed; have = true; }
            const int margins[5] = {0, 16, 32, 48, 64}; long dry[5] = {}, open[5] = {}, byHeight[32] = {}; double sum = 0, lo = 1e9, hi = -1e9;
            for (int r = 0; r < NW; r++) for (int c = 0; c < NW; c++) {
                double x = s.x - 8 * NW + 16 * c + 8, z = s.z - 8 * NW + 16 * r + 8;
                int64_t np[6]; sampleBiomeNoise(&bn, np, (int) floor(x / 4), 0, (int) floor(z / 4), NULL, SAMPLE_NO_BIOME);
                double surface = 128.0 * np[4] / 10000.0; sum += surface; lo = std::min(lo, surface); hi = std::max(hi, surface);
                for (int level = 0; level < 32; level++) {
                    int y = -48 + 8 * level;
                    float v = cheese.sample(x, y * 0.6666666666666666, z) + 0.27f;
                    if (v >= 0) continue;
                    if (v < -1) v = -1;
                    float ly = layer.sample(x, y * 8.0, z);
                    if (v + 4 * ly * ly >= 0) continue;
                    bool isDry = flood.sample(x, y * 0.67, z) <= 0.4f;
                    for (int m = 0; m < 5; m++) if (y <= surface - margins[m]) { open[m]++; dry[m] += isDry; }
                    if (isDry && y <= surface) byHeight[level]++;
                }
            }
            char buf[1024]; int n = snprintf(buf, sizeof buf, "%lld %ld %ld | %.1f %.1f %.1f |", (long long) s.seed, s.x, s.z, sum / (NW * NW), lo, hi);
            for (int m = 0; m < 5; m++) n += snprintf(buf + n, sizeof buf - n, " %ld %ld", dry[m], open[m]);
            n += snprintf(buf + n, sizeof buf - n, " |");
            for (int level = 0; level < 32; level++) n += snprintf(buf + n, sizeof buf - n, " %ld", byHeight[level]);
            out[k] = buf;
        }
    });
    for (auto &t : pool) t.join();
    for (auto &s : out) puts(s.c_str());
}
