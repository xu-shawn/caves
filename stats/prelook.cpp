// Gate probe: how well do cheap partial looks predict the gate's final score? For seeds on stdin, the full numbers
// (512-block gate, 1280-block gate, percolation volume) and partial ones from octave -8 alone (both stacks):
//   m256: lowest 1280-block window mean of stack A + that of stack B, windows 256 blocks apart
//   q256: the pair number (62nd pair) of the same fields
//   m512: as m256 with windows 512 blocks apart (a quarter of the windows)
//   g8:   the 512-block pair number (pair 11,544) of octave -8 alone, windows 256 blocks apart
// and the same four with both octaves (m256b, q256b, m512b; q256b is the first look in use).
// build: g++ -O3 -march=native -std=c++17 -I . -I vendor/cubiomes stats/prelook.cpp vendor/cub_noise.o -lpthread -o stats/prelook
#include "percgate.hpp"
#include <atomic>
#include <cstdio>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
int main(int argc, char **argv) {
    int threads = argc > 1 ? atoi(argv[1]) : 8;
    std::vector<uint64_t> seeds; std::string l; while (std::getline(std::cin, l)) if (!l.empty()) seeds.push_back((uint64_t) std::stoll(l));
    std::atomic<size_t> next(0); std::mutex lock; std::vector<std::thread> pool;
    for (int t = 0; t < threads; t++) pool.emplace_back([&] {
        AnalyticGate gate, low; Percolation perc; const int N = AnalyticGate::N; std::vector<float> a(256 * 256), b(256 * 256);
        auto partial = [&](AnalyticGate &g, float *out) { // m256, q256, m512, g512 at 256 from g's fields
            float lowest; g.wideValue(); float mA = 1, mB = 1, cA = 1, cB = 1;
            for (int i = 0; i < 256; i++) for (int j = 0; j < 256; j++) {
                float x = g.wide[0][(size_t) (2 * i) * N + 2 * j], y = g.wide[1][(size_t) (2 * i) * N + 2 * j]; a[i * 256 + j] = x; b[i * 256 + j] = y; mA = std::min(mA, x); mB = std::min(mB, y);
                if (i % 2 == 0 && j % 2 == 0) { cA = std::min(cA, x); cB = std::min(cB, y); }
            }
            out[0] = mA + mB; out[1] = g.quantile(a, b, 62, lowest); out[2] = cA + cB;
            for (int i = 0; i < 256; i++) for (int j = 0; j < 256; j++) { a[i * 256 + j] = g.field[0][(size_t) (2 * i) * N + 2 * j]; b[i * 256 + j] = g.field[1][(size_t) (2 * i) * N + 2 * j]; }
            out[3] = g.quantile(a, b, 11544, lowest);
        };
        for (size_t k; (k = next++) < seeds.size();) {
            NormalNoise cheese = makeNoise(seeds[k], "minecraft:cave_cheese"); float lowest, value = gate.value(cheese, lowest), wide = gate.wideValue(), volume = perc.volume(gate), p8[4], pb[4];
            partial(gate, pb);
            for (int half = 0; half < 2; half++) { std::fill(low.field[half].begin(), low.field[half].end(), 0.0f); low.addLayer(cheese.layers[half], 0.6666666666666666, 0.5, low.field[half]); }
            partial(low, p8);
            std::lock_guard<std::mutex> guard(lock);
            printf("%lld %.4f %.4f %.4f  %.4f %.4f %.4f %.4f  %.4f %.4f %.4f %.4f\n", (long long) seeds[k], value, wide, volume, p8[0], p8[1], p8[2], p8[3], pb[0], pb[1], pb[2], pb[3]);
        }
    });
    for (auto &t : pool) t.join();
}
