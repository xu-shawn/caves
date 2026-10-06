// The cave_layer term around a cavern: the cheese formula opens a block where clamp(0.27 + cave_cheese) + 4 cave_layer^2
// is below 0, cave_layer being one octave pair on 256-block cells sampled at 8 x the height. For "seed x z ..." lines on
// stdin: the mean of 4 cave_layer^2 over square rings around the site (0-320, 320-640, 640-960, 960-1280 blocks) for
// y -40..40 and y 40..120, at the site and, as a baseline, at a place 9,000 blocks away in x and z; and the share of
// samples under 0.05 (where the term does not close a cave that the cheese opens), same layout.
// build: g++ -O3 -march=native -std=c++17 -I . -I vendor/cubiomes stats/layerfeatures.cpp vendor/cub_noise.o -lpthread -o stats/layerfeatures
// usage: layerfeatures [THREADS=4] < sites      prints: seed x z, then 32 numbers: (mean, share) x (site, baseline) x band (2) x ring (4)
#include "cavenoise.hpp"
#include <atomic>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
int main(int argc, char **argv) {
    int threads = argc > 1 ? atoi(argv[1]) : 4;
    std::vector<std::string> lines; std::string line;
    while (std::getline(std::cin, line)) lines.push_back(line);
    std::atomic<size_t> next(0); std::mutex print; std::vector<std::thread> pool;
    static const int STEP = 40, HALF = 32, RINGS = 4;
    for (int t = 0; t < threads; t++) pool.emplace_back([&] {
        for (size_t k; (k = next++) < lines.size();) {
            long long seed; double x, z;
            if (sscanf(lines[k].c_str(), "%lld %lf %lf", &seed, &x, &z) < 3) continue;
            NormalNoise layer = makeNoise((uint64_t) seed, "minecraft:cave_layer");
            double mean[2][2][RINGS] = {}, share[2][2][RINGS] = {}; long count[RINGS] = {};
            for (int place = 0; place < 2; place++) for (int iz = -HALF; iz < HALF; iz++) for (int ix = -HALF; ix < HALF; ix++) {
                const int ring = std::min(RINGS - 1, std::max(std::abs(ix), std::abs(iz)) * STEP / 320); if (!place) count[ring]++;
                const double px = x + ix * STEP + 9000.0 * place, pz = z + iz * STEP + 9000.0 * place;
                for (int band = 0; band < 2; band++) for (int iy = 0; iy < 27; iy++) { // every 3 blocks: the term turns over every 32 or so
                    const double y = (band ? 40 : -40) + 3 * iy; float v = layer.sample(px, y * 8.0, pz), term = 4 * v * v;
                    mean[place][band][ring] += term; share[place][band][ring] += term < 0.05f;
                }
            }
            std::string out = std::to_string(seed) + " " + std::to_string((long) x) + " " + std::to_string((long) z); char buf[32];
            for (int what = 0; what < 2; what++) for (int place = 0; place < 2; place++) for (int band = 0; band < 2; band++) for (int r = 0; r < RINGS; r++) { snprintf(buf, sizeof buf, " %.4f", (what ? share : mean)[place][band][r] / (27.0 * count[r])); out += buf; }
            std::lock_guard<std::mutex> guard(print); puts(out.c_str());
        }
    });
    for (auto &t : pool) t.join();
}
