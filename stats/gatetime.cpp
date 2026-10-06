// Where the full gate's time goes: makeNoise, AnalyticGate::value, wideValue, Percolation::volume, per seed.
// build: g++ -O3 -march=native -std=c++17 -I . -I vendor/cubiomes stats/gatetime.cpp vendor/cub_noise.o -o stats/gatetime
#include "percgate.hpp"
#include <chrono>
#include <memory>
static double now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
int main(int argc, char **argv) {
    int n = argc > 1 ? atoi(argv[1]) : 100; uint64_t x = 0x9e3779b97f4a7c15ULL; double t[4] = {0}, q[3] = {0}, sum = 0;
    std::unique_ptr<AnalyticGate> gate(new AnalyticGate); std::unique_ptr<Percolation> perc(new Percolation);
    for (int i = 0; i < n; i++) {
        x ^= x << 13; x ^= x >> 7; x ^= x << 17; double a = now();
        NormalNoise cheese = makeNoise(x >> 16, "minecraft:cave_cheese"); double b = now();
        float lowest, value = gate->value(cheese, lowest); double c = now();
        float wide = gate->wideValue(); double d = now();
        float volume = perc->volume(*gate); double e = now();
        float low2; double f0 = now(); gate->quantile(gate->field[0], gate->field[1], AnalyticGate::PAIRS, low2); double f1 = now(); gate->quantile(gate->wide[0], gate->wide[1], AnalyticGate::WIDE_PAIRS, low2); double f2 = now(); perc->spots(perc->wa, perc->sa); double f3 = now();
        q[0] += f1 - f0; q[1] += f2 - f1; q[2] += f3 - f2;
        t[0] += b - a; t[1] += c - b; t[2] += d - c; t[3] += e - d; sum += value + wide + volume;
        if (i < 3) printf("seed %llu: gate512 %.4f wide %.4f volume %.4f\n", (unsigned long long) (x >> 16), value, wide, volume);
    }
    printf("of which: the pair level of value %.2f ms, of wideValue %.2f ms; one of the two spot searches of the volume %.2f ms\n", 1e3 * q[0] / n, 1e3 * q[1] / n, 1e3 * q[2] / n);
    printf("%d seeds: makeNoise %.2f ms, value %.2f ms, wideValue %.2f ms, percolation volume %.2f ms (checksum %.4f)\n", n, 1e3 * t[0] / n, 1e3 * t[1] / n, 1e3 * t[2] / n, 1e3 * t[3] / n, sum);
}
