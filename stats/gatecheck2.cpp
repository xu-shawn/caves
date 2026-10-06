// The full gate's numbers, new code against the first version (analyticgate_v1.hpp, percgate_v1.hpp kept under other names
// in a namespace): gate512, wide and volume for N seeds, the largest differences, and the time of each part.
// build: g++ -O3 -march=native -std=c++17 -I . -I vendor/cubiomes stats/gatecheck2.cpp vendor/cub_noise.o -o stats/gatecheck2
#include "percgate.hpp"
#include <chrono>
#include <memory>
namespace v1 {
#define AnalyticGate AnalyticGateV1
#define Percolation PercolationV1
#include "../percgate_v1_body.inc"
#undef AnalyticGate
#undef Percolation
}
static double now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
int main(int argc, char **argv) {
    int n = argc > 1 ? atoi(argv[1]) : 200; uint64_t x = 0x9e3779b97f4a7c15ULL; double t[2][3] = {{0}}, worst[3] = {0}; int differ[3] = {0};
    std::unique_ptr<AnalyticGate> gate(new AnalyticGate); std::unique_ptr<Percolation> perc(new Percolation);
    std::unique_ptr<v1::AnalyticGateV1> gate1(new v1::AnalyticGateV1); std::unique_ptr<v1::PercolationV1> perc1(new v1::PercolationV1);
    for (int i = 0; i < n; i++) {
        x ^= x << 13; x ^= x >> 7; x ^= x << 17; NormalNoise cheese = makeNoise(x >> 16, "minecraft:cave_cheese"); float lowest, a[3], b[3];
        double t0 = now(); a[0] = gate->value(cheese, lowest); double t1 = now(); a[1] = gate->wideValue(); double t2 = now(); a[2] = perc->volume(*gate); double t3 = now();
        b[0] = gate1->value(cheese, lowest); double t4 = now(); b[1] = gate1->wideValue(); double t5 = now(); b[2] = perc1->volume(*gate1); double t6 = now();
        t[0][0] += t1 - t0; t[0][1] += t2 - t1; t[0][2] += t3 - t2; t[1][0] += t4 - t3; t[1][1] += t5 - t4; t[1][2] += t6 - t5;
        for (int k = 0; k < 3; k++) { worst[k] = std::max(worst[k], (double) std::fabs(a[k] - b[k])); differ[k] += std::fabs(a[k] - b[k]) > 1e-5f; }
        if (i < 2) printf("seed %llu: new %.5f %.5f %.5f, first version %.5f %.5f %.5f\n", (unsigned long long) (x >> 16), a[0], a[1], a[2], b[0], b[1], b[2]);
    }
    const char *names[3] = {"value (gate512)", "wideValue", "percolation volume"};
    for (int k = 0; k < 3; k++) printf("%-20s first version %6.2f ms, new %6.2f ms (x%.1f); largest difference %.2e, more than 1e-5 apart in %d of %d seeds\n", names[k], 1e3 * t[1][k] / n, 1e3 * t[0][k] / n, t[1][k] / t[0][k], worst[k], differ[k], n);
}
