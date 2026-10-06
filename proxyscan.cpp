// The bare cheese-cave formula over a big area: for every STRIDE-th column, how many of the heights Y_MIN, Y_MIN + Y_STEP,
// ..., Y_MAX have clamp(0.27 + cave_cheese, -1, 1) + 4 cave_layer^2 < 0 (no terrain, aquifers or interpolation).
// Writes one byte per column, row by row (z outer), to OUT.
// build: g++ -O3 -march=native -std=c++17 -I vendor/cubiomes proxyscan.cpp vendor/cub_noise.o -lpthread -o proxyscan
// usage: proxyscan SEED X0 Z0 SIZE_X SIZE_Z STRIDE Y_MIN Y_MAX Y_STEP OUT [THREADS]
#include "cavenoise.hpp"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>
int main(int argc, char **argv) {
    uint64_t seed = strtoull(argv[1], 0, 10); long x0 = atol(argv[2]), z0 = atol(argv[3]); int sx = atoi(argv[4]), sz = atoi(argv[5]), stride = atoi(argv[6]), y0 = atoi(argv[7]), y1 = atoi(argv[8]), ys = atoi(argv[9]);
    int threads = argc > 11 ? atoi(argv[11]) : 8, nx = sx / stride, nz = sz / stride;
    NormalNoise cheese = makeNoise(seed, "minecraft:cave_cheese"), layer = makeNoise(seed, "minecraft:cave_layer");
    std::vector<uint8_t> out((size_t) nx * nz); std::atomic<int> next(0); std::vector<std::thread> pool;
    for (int t = 0; t < threads; t++) pool.emplace_back([&] {
        for (int row; (row = next++) < nz;) {
            double z = (double) (z0 + (long) row * stride);
            for (int col = 0; col < nx; col++) {
                double x = (double) (x0 + (long) col * stride); int count = 0;
                for (int y = y0; y <= y1; y += ys) {
                    float c = cheese.sample(x, y * 0.6666666666666666, z) + 0.27f; c = c < -1 ? -1 : c > 1 ? 1 : c;
                    if (c >= 0) continue;
                    float l = layer.sample(x, y * 8.0, z);
                    count += c + 4 * l * l < 0;
                }
                out[(size_t) row * nx + col] = (uint8_t) count;
            }
        }
    });
    for (auto &t : pool) t.join();
    FILE *f = fopen(argv[10], "wb"); fwrite(out.data(), 1, out.size(), f); fclose(f);
    fprintf(stderr, "%d x %d columns\n", nx, nz);
}
