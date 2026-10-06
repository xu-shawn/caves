// What the low layers of the cave noise look like around a cavern, ring by ring: for "seed x z ..." lines on stdin
// (x, z: the middle of the cavern), the mean of each of the 8 lowest cave_cheese layers (octaves -8, -7, -6, -5, stacks
// A and B, with their amplitudes) over square rings around the site (Chebyshev distance 0-320, 320-640, ... 1600-1920
// blocks) and over two height bands (y -40..40 and 80..160). Sampled every 40 blocks (not a divisor of the cells, so the
// samples do not sit on lattice points). For the question of how far out the noise still decides a cavern's size.
// build: g++ -O3 -march=native -std=c++17 -I . -I vendor/cubiomes stats/sitefeatures.cpp vendor/cub_noise.o -lpthread -o stats/sitefeatures
// usage: sitefeatures [THREADS=4] < sites      prints: seed x z, then 96 numbers: layer (A-8, B-8, A-7, B-7, A-6, B-6, A-5, B-5) x band (low, high) x ring (6)
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
    static const int STEP = 40, HALF = 48, RINGS = 6, LAYERS = 8; static const double YS[2][3] = {{-40, 0, 40}, {80, 120, 160}};
    for (int t = 0; t < threads; t++) pool.emplace_back([&] {
        for (size_t k; (k = next++) < lines.size();) {
            long long seed; double x, z;
            if (sscanf(lines[k].c_str(), "%lld %lf %lf", &seed, &x, &z) < 3) continue;
            NormalNoise cheese = makeNoise((uint64_t) seed, "minecraft:cave_cheese");
            double sum[LAYERS][2][RINGS] = {}; long count[RINGS] = {};
            for (int iz = -HALF; iz <= HALF; iz++) for (int ix = -HALF; ix <= HALF; ix++) {
                int ring = std::min(RINGS - 1, std::max(std::abs(ix), std::abs(iz)) * STEP / 320); count[ring]++;
                double px = x + ix * STEP, pz = z + iz * STEP;
                for (int l = 0; l < LAYERS; l++) {
                    const NoiseLayer &layer = cheese.layers[l]; ColumnPerlin column(&layer.perlin, px * layer.frequency, pz * layer.frequency);
                    for (int band = 0; band < 2; band++) { double v = 0; for (double y : YS[band]) v += column.at(y * 0.6666666666666666 * layer.frequency); sum[l][band][ring] += layer.amplitude * v / 3; }
                }
            }
            std::string out = std::to_string(seed) + " " + std::to_string((long) x) + " " + std::to_string((long) z); char buf[32];
            for (int l = 0; l < LAYERS; l++) for (int band = 0; band < 2; band++) for (int r = 0; r < RINGS; r++) { snprintf(buf, sizeof buf, " %.5f", sum[l][band][r] / count[r]); out += buf; }
            std::lock_guard<std::mutex> guard(print); puts(out.c_str());
        }
    });
    for (auto &t : pool) t.join();
}
