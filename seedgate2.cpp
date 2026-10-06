// A cheap look at a seed before the whole search runs on it: how low does the cave_cheese noise get, averaged over a
// 518-block window, for its A stack and its B stack?
//
// The biggest caverns of a seed sit where both stacks are low at once (cavepairs lists those places), and how big they
// get follows how low the two stacks can go. The gate value is the summed window mean (A + B) at the pair count that
// matches cavepairs' cut; it only needs the two lowest octaves (the higher ones average out over the window) on a coarse
// grid: one period of each stack, 506 x 506 columns, 5 heights.
// In front of it, the same number on a grid twice as coarse (253 x 253 columns, 3 heights, 2 x 2 windows): a sixth of the
// work; only seeds whose coarse value is at most PRE go on to the real gate.
// Prints "seed gate lowest pre" for the seeds that pass the coarse look.
//
// build: g++ -O3 -march=native -std=c++17 -I vendor/cubiomes seedgate2.cpp vendor/cub_noise.o -lpthread -o seedgate2
// usage: seedgate2 FIRST LAST [THREADS=8] [PRE=-0.400]      or      seedgate2 - - [THREADS] [PRE] < seeds        (PRE 9: every seed)
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
int main(int argc, char **argv) {
    int threads = argc > 3 ? atoi(argv[3]) : 8; float pre = argc > 4 ? atof(argv[4]) : -0.400f;
    std::vector<uint64_t> seeds; bool range = argc > 2 && argv[1][0] != '-';
    uint64_t first = range ? strtoull(argv[1], 0, 10) : 0, last = range ? strtoull(argv[2], 0, 10) : 0;
    if (!range) { std::string l; while (std::getline(std::cin, l)) if (!l.empty()) seeds.push_back((uint64_t) std::stoll(l)); }
    size_t count = range ? (size_t) (last - first + 1) : seeds.size();
    std::atomic<size_t> next(0); std::mutex lock; std::vector<std::thread> pool;
    for (int t = 0; t < threads; t++) pool.emplace_back([&] {
        Gate coarse(253, 2, 3, 64, 10625, 8), full(506, 4, 5, 32, 170000, 1);
        for (size_t k; (k = next++) < count;) {
            uint64_t seed = range ? first + k : seeds[k];
            NormalNoise cheese = makeNoise(seed, "minecraft:cave_cheese");
            float lowest, coarseValue = coarse.value(cheese, lowest);
            if (coarseValue > pre) continue;
            float value = full.value(cheese, lowest);
            std::lock_guard<std::mutex> guard(lock);
            printf("%lld %.4f %.4f %.4f\n", (long long) seed, value, lowest, coarseValue); fflush(stdout);
        }
    });
    for (auto &t : pool) t.join();
}
