// Speed and result check of the GPU phase scan: scanbench [BATCHES=20] -> seeds a second, survivors, and a checksum of them
// build: g++ -O3 -std=c++17 -I . -I vendor/cubiomes stats/scanbench.cpp gpuscan.o vendor/cub_noise.o -L/usr/local/cuda/lib64 -lcudart -lpthread -o scanbench
#include "cavenoise.hpp"
#include "gpuscan.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>
int main(int argc, char **argv) {
    const int batches = argc > 1 ? atoi(argv[1]) : 20; const double c8 = 0.542, w8 = 0.045, c7 = 0.584, w7 = 0.10;
    static const double AT[4] = {16.0 / 256, 16.0 / 128, 16.0 / 256 * 1.0181268882175227, 16.0 / 128 * 1.0181268882175227};
    const NameHash cheeseHash = nameHash("minecraft:cave_cheese"), o8 = nameHash("octave_-8"), o7 = nameHash("octave_-7");
    GpuScanWindows w = {cheeseHash.lo, cheeseHash.hi, o8.lo, o8.hi, o7.lo, o7.hi, {0, 0, 0, 0}, {0, 0, 0, 0}};
    for (int i = 0; i < 4; i++) { double centre = i % 2 ? c7 : c8, width = i % 2 ? w7 : w8, lo = centre - width - AT[i], hi = centre + width - AT[i]; w.low[i] = (int64_t) (lo * 35184372088832.0) - (1 << 20); w.high[i] = (int64_t) (hi * 35184372088832.0) + (1 << 20); }
    const uint32_t THREADS = 1 << 20, CAPACITY = 1 << 20; const int PER = 1024;
    if (gpuscan_init(&w, CAPACITY)) return 1;
    std::vector<uint64_t> found(CAPACITY); uint64_t base = 5000000000000000ULL, sum = 0, total = 0;
    auto now = [] { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); };
    gpuscan_batch(base - (uint64_t) THREADS * PER, THREADS, PER, found.data()); double t0 = now();
    for (int b = 0; b < batches; b++) { long n = gpuscan_batch(base, THREADS, PER, found.data()); if (n < 0) return 1; total += n; for (long i = 0; i < n; i++) sum += found[i] * 0x9e3779b97f4a7c15ULL ^ (found[i] >> 7); base += (uint64_t) THREADS * PER; }
    double s = now() - t0; printf("%.3g seeds a second, %llu survivors, checksum %016llx\n", (double) batches * THREADS * PER / s, (unsigned long long) total, (unsigned long long) sum);
}
