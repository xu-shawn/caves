// Tail probe for the seed gate: for seeds on stdin, the table-gate statistic at several depths and window sizes.
// The gate in use is the window-mean sum (two lowest cave_cheese octaves, 512-block windows) at pair number 184,700.
// Here: the same at pair numbers 10, 100, 1,000, 10,000, 184,700 and 1,000,000, for windows of 512 blocks, of
// 1024 blocks (the mean of 2 x 2 windows side by side) and of 1536 blocks (3 x 3).
// build: g++ -O3 -march=native -std=c++17 -I . -I vendor/cubiomes stats/gatefeatures.cpp vendor/cub_noise.o -lpthread -o stats/gatefeatures
// usage: gatefeatures [THREADS=8] < seeds        prints: seed, then 6 values per window size
// gatefeatures2 (-DSHAPES): other window shapes at pair numbers 1,000 and 10,000: 768 and 1280 (tapered), 1024, 1536, and 1024 x 2048 (the better of the two directions)
#include "analyticgate.hpp"
#include <atomic>
#include <cstdio>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
static const int N = AnalyticGate::N, BINS = 8192; static const float PER_UNIT = 4096.0f;
static const uint64_t COUNTS[6] = {10, 100, 1000, 10000, 184700, 1000000};
static void quantiles(const std::vector<float> &a, const std::vector<float> &b, float *out) {
    float lowA = a[0], lowB = b[0];
    for (float v : a) lowA = std::min(lowA, v);
    for (float v : b) lowB = std::min(lowB, v);
    std::vector<uint64_t> ha(BINS, 0), hb(BINS, 0);
    for (float v : a) { int bin = (int) ((v - lowA) * PER_UNIT); if (bin < BINS) ha[bin]++; }
    for (float v : b) { int bin = (int) ((v - lowB) * PER_UNIT); if (bin < BINS) hb[bin]++; }
    for (int i = 1; i < BINS; i++) hb[i] += hb[i - 1];
    for (int q = 0; q < 6; q++) {
        int lo = 0, hi = BINS - 1;
        while (lo < hi) {
            int mid = (lo + hi) / 2; uint64_t pairs = 0;
            for (int i = 0; i <= mid; i++) if (ha[i]) pairs += ha[i] * hb[mid - i];
            if (pairs >= COUNTS[q]) hi = mid; else lo = mid + 1;
        }
        out[q] = lowA + lowB + (lo + 1) / PER_UNIT;
    }
}

#ifdef SHAPES
static void shaped(const std::vector<float> &in, std::vector<float> &out, const int *ox, int nx, const int *oz, int nz) {
    for (int i = 0; i < N; i++) for (int j = 0; j < N; j++) {
        float s = 0;
        for (int a = 0; a < nx; a++) for (int b = 0; b < nz; b++) s += in[(size_t) ((i + ox[a]) % N) * N + (j + oz[b]) % N];
        out[(size_t) i * N + j] = s / (nx * nz);
    }
}
int main(int argc, char **argv) {
    int threads = argc > 1 ? atoi(argv[1]) : 8;
    std::vector<uint64_t> seeds; std::string l; while (std::getline(std::cin, l)) if (!l.empty()) seeds.push_back((uint64_t) std::stoll(l));
    std::atomic<size_t> next(0); std::mutex lock; std::vector<std::thread> pool;
    static const int O768[2] = {0, 2}, O1024[2] = {0, 4}, O1280[4] = {0, 2, 4, 6}, O1536[3] = {0, 4, 8}, O2048[4] = {0, 4, 8, 12};
    for (int t = 0; t < threads; t++) pool.emplace_back([&] {
        AnalyticGate gate; std::vector<float> wide[2]; wide[0].resize((size_t) N * N); wide[1].resize((size_t) N * N);
        for (size_t k; (k = next++) < seeds.size();) {
            NormalNoise cheese = makeNoise(seeds[k], "minecraft:cave_cheese"); float lowest, q[6], out[12]; int n = 0;
            gate.value(cheese, lowest);
            auto one = [&](const int *ox, int nx, const int *oz, int nz) { for (int half = 0; half < 2; half++) shaped(gate.field[half], wide[half], ox, nx, oz, nz); quantiles(wide[0], wide[1], q); };
            one(O768, 2, O768, 2); out[n++] = q[2]; out[n++] = q[3];
            one(O1024, 2, O1024, 2); out[n++] = q[2]; out[n++] = q[3];
            one(O1280, 4, O1280, 4); out[n++] = q[2]; out[n++] = q[3];
            one(O1536, 3, O1536, 3); out[n++] = q[2]; out[n++] = q[3];
            one(O1024, 2, O2048, 4); float a2 = q[2], a3 = q[3]; one(O2048, 4, O1024, 2); out[n++] = std::min(a2, q[2]); out[n++] = std::min(a3, q[3]);
            one(O1024, 2, O1536, 3); a2 = q[2]; a3 = q[3]; one(O1536, 3, O1024, 2); out[n++] = std::min(a2, q[2]); out[n++] = std::min(a3, q[3]);
            std::lock_guard<std::mutex> guard(lock);
            printf("%lld", (long long) seeds[k]); for (int i = 0; i < n; i++) printf(" %.4f", out[i]); printf("\n");
        }
    });
    for (auto &t : pool) t.join();
}
#else
int main(int argc, char **argv) {
    int threads = argc > 1 ? atoi(argv[1]) : 8;
    std::vector<uint64_t> seeds; std::string l; while (std::getline(std::cin, l)) if (!l.empty()) seeds.push_back((uint64_t) std::stoll(l));
    std::atomic<size_t> next(0); std::mutex lock; std::vector<std::thread> pool;
    for (int t = 0; t < threads; t++) pool.emplace_back([&] {
        AnalyticGate gate; std::vector<float> wide[2]; wide[0].resize((size_t) N * N); wide[1].resize((size_t) N * N);
        for (size_t k; (k = next++) < seeds.size();) {
            NormalNoise cheese = makeNoise(seeds[k], "minecraft:cave_cheese"); float lowest, out[18];
            gate.value(cheese, lowest); quantiles(gate.field[0], gate.field[1], out);
            for (int scale = 2; scale <= 3; scale++) {
                for (int half = 0; half < 2; half++) for (int i = 0; i < N; i++) for (int j = 0; j < N; j++) {
                    float s = 0;
                    for (int a = 0; a < scale; a++) for (int b = 0; b < scale; b++) s += gate.field[half][(size_t) ((i + 4 * a) % N) * N + (j + 4 * b) % N];
                    wide[half][(size_t) i * N + j] = s / (scale * scale);
                }
                quantiles(wide[0], wide[1], out + 6 * (scale - 1));
            }
            std::lock_guard<std::mutex> guard(lock);
            printf("%lld", (long long) seeds[k]); for (int i = 0; i < 18; i++) printf(" %.4f", out[i]); printf("\n");
        }
    });
    for (auto &t : pool) t.join();
}
#endif
