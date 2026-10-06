// Wider windows for the seed gate: for seeds on stdin, the pair statistic of analyticgate.hpp (the level under which
// 1,000 pairs of an A window and a B window fall) for tapered square windows of 1280 (the gate's wide number), 1536,
// 1792, 2048, 2304 and 2816 blocks (n x n of the 512-block windows, 256 blocks apart, n = 4 .. 10), and for 1280 x 2304
// (the better direction). For the question whether the caverns of 46M+, which are 1,700 x 2,200 blocks, want a wider window.
// build: g++ -O3 -march=native -std=c++17 -I . -I vendor/cubiomes stats/gatefeatures3.cpp vendor/cub_noise.o -lpthread -o stats/gatefeatures3
// usage: gatefeatures3 [THREADS=4] < seeds        prints: seed, then 7 values
#include "analyticgate.hpp"
#include <atomic>
#include <cstdio>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
static const int N = AnalyticGate::N, BINS = 8192; static const float PER_UNIT = 4096.0f;
static float pairLevel(const std::vector<float> &a, const std::vector<float> &b, uint64_t count) {
    float lowA = a[0], lowB = b[0];
    for (float v : a) lowA = std::min(lowA, v);
    for (float v : b) lowB = std::min(lowB, v);
    std::vector<uint64_t> ha(BINS, 0), hb(BINS, 0);
    for (float v : a) { int bin = (int) ((v - lowA) * PER_UNIT); if (bin < BINS) ha[bin]++; }
    for (float v : b) { int bin = (int) ((v - lowB) * PER_UNIT); if (bin < BINS) hb[bin]++; }
    for (int i = 1; i < BINS; i++) hb[i] += hb[i - 1];
    int lo = 0, hi = BINS - 1;
    while (lo < hi) {
        int mid = (lo + hi) / 2; uint64_t pairs = 0;
        for (int i = 0; i <= mid; i++) if (ha[i]) pairs += ha[i] * hb[mid - i];
        if (pairs >= count) hi = mid; else lo = mid + 1;
    }
    return lowA + lowB + (lo + 1) / PER_UNIT;
}
// out = the mean of nx x nz of the 512-block windows of `in`, 2 grid steps (256 blocks) apart: along x, then along z
static void shaped(const std::vector<float> &in, std::vector<float> &scratch, std::vector<float> &out, int nx, int nz) {
    for (int i = 0; i < N; i++) for (int j = 0; j < N; j++) { float s = 0; for (int a = 0; a < nx; a++) s += in[(size_t) ((i + 2 * a) % N) * N + j]; scratch[(size_t) i * N + j] = s; }
    for (int i = 0; i < N; i++) for (int j = 0; j < N; j++) { float s = 0; for (int b = 0; b < nz; b++) s += scratch[(size_t) i * N + (j + 2 * b) % N]; out[(size_t) i * N + j] = s / (nx * nz); }
}
int main(int argc, char **argv) {
    int threads = argc > 1 ? atoi(argv[1]) : 4;
    std::vector<uint64_t> seeds; std::string l; while (std::getline(std::cin, l)) if (!l.empty()) seeds.push_back((uint64_t) std::stoll(l));
    std::atomic<size_t> next(0); std::mutex lock; std::vector<std::thread> pool;
    for (int t = 0; t < threads; t++) pool.emplace_back([&] {
        AnalyticGate gate; std::vector<float> wide[2], scratch((size_t) N * N); wide[0].resize((size_t) N * N); wide[1].resize((size_t) N * N);
        for (size_t k; (k = next++) < seeds.size();) {
            NormalNoise cheese = makeNoise(seeds[k], "minecraft:cave_cheese"); float lowest, out[7]; int n = 0;
            gate.value(cheese, lowest);
            auto one = [&](int nx, int nz) { for (int half = 0; half < 2; half++) shaped(gate.field[half], scratch, wide[half], nx, nz); return pairLevel(wide[0], wide[1], 1000); };
            for (int size : {4, 5, 6, 7, 8, 10}) out[n++] = one(size, size);
            out[n++] = std::min(one(4, 8), one(8, 4));
            std::lock_guard<std::mutex> guard(lock);
            printf("%lld", (long long) seeds[k]); for (int i = 0; i < n; i++) printf(" %.4f", out[i]); printf("\n"); fflush(stdout);
        }
    });
    for (auto &t : pool) t.join();
}
