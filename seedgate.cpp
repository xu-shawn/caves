// A cheap look at a seed before the whole search runs on it: how low does the cave_cheese noise get, averaged over a
// 518-block window, for its A stack and its B stack?
//
// The biggest caverns of a seed sit where both stacks are low at once (cavepairs lists those places), and how big they
// get follows how low the two stacks can go: over 82 seeds the best formula cavern has a correlation of -0.87 with the
// summed window mean at the 300,000th pair. That number only needs the two lowest octaves (the higher ones average out
// over the window) on a coarse grid: one period of each stack, 506 x 506 columns, 5 heights.
// Prints "seed cut lowest": the window-mean sum at the pair count that matches cavepairs' cut, and the lowest sum.
//
// build: g++ -O3 -march=native -std=c++17 -I vendor/cubiomes seedgate.cpp vendor/cub_noise.o -lpthread -o seedgate
// usage: seedgate FIRST LAST [THREADS=8]        or        seedgate - - [THREADS] < seeds
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
static const int N = 506, WINDOW = 4, LEVELS = 5, OCTAVES = 2;
static const size_t PAIRS = 170000; // the share of all pairs that cavepairs keeps (300,000 of 1011^2 x 993^2 / 9)
int main(int argc, char **argv) {
    int threads = argc > 3 ? atoi(argv[3]) : 8;
    std::vector<uint64_t> seeds; bool range = argc > 2 && argv[1][0] != '-';
    uint64_t first = range ? strtoull(argv[1], 0, 10) : 0, last = range ? strtoull(argv[2], 0, 10) : 0;
    if (!range) { std::string l; while (std::getline(std::cin, l)) if (!l.empty()) seeds.push_back((uint64_t) std::stoll(l)); }
    size_t count = range ? (size_t) (last - first + 1) : seeds.size();
    std::atomic<size_t> next(0); std::mutex lock; std::vector<std::thread> pool;
    for (int t = 0; t < threads; t++) pool.emplace_back([&] {
        std::vector<float> column((size_t) N * N), rows((size_t) N * N), mean[2]; mean[0].resize((size_t) N * N); mean[1].resize((size_t) N * N);
        for (size_t k; (k = next++) < count;) {
            uint64_t seed = range ? first + k : seeds[k];
            NormalNoise cheese = makeNoise(seed, "minecraft:cave_cheese");
            for (int half = 0; half < 2; half++) {
                double period = half ? 65536.0 * 331 / 337 : 65536.0, step = period / N;
                for (int i = 0; i < N; i++) for (int j = 0; j < N; j++) {
                    double x = i * step, z = j * step; float sum = 0;
                    for (int level = 0; level < LEVELS; level++) {
                        double y = (-40 + 32 * level) * 0.6666666666666666;
                        for (int o = 0; o < OCTAVES; o++) { const NoiseLayer &l = cheese.layers[2 * o + half]; sum += l.amplitude * (float) fastPerlin(&l.perlin, x * l.frequency, y * l.frequency, z * l.frequency); }
                    }
                    column[(size_t) i * N + j] = sum;
                }
                for (int i = 0; i < N; i++) for (int j = 0; j < N; j++) { float s = 0; for (int d = 0; d < WINDOW; d++) s += column[(size_t) i * N + (j + d) % N]; rows[(size_t) i * N + j] = s; }
                for (int i = 0; i < N; i++) for (int j = 0; j < N; j++) { float s = 0; for (int d = 0; d < WINDOW; d++) s += rows[(size_t) ((i + d) % N) * N + j]; mean[half][(size_t) i * N + j] = s / (WINDOW * WINDOW * LEVELS); }
                std::sort(mean[half].begin(), mean[half].end());
            }
            auto pairsBelow = [&](float threshold) {
                size_t total = 0;
                for (float a : mean[0]) {
                    float limit = threshold - a;
                    if (limit < mean[1][0]) break;
                    total += std::upper_bound(mean[1].begin(), mean[1].end(), limit) - mean[1].begin();
                    if (total > 4 * PAIRS) break;
                }
                return total;
            };
            float lowest = mean[0][0] + mean[1][0], lo = lowest, hi = 0;
            for (int it = 0; it < 30; it++) { float mid = 0.5f * (lo + hi); (pairsBelow(mid) >= PAIRS ? hi : lo) = mid; }
            std::lock_guard<std::mutex> guard(lock);
            printf("%lld %.4f %.4f\n", (long long) seed, hi, lowest); fflush(stdout);
        }
    });
    for (auto &t : pool) t.join();
}
