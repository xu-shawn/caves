// icecaves/icepatch.cpp: how many large patches of ice caves a world has, for seeds on stdin ("SEED ..." per line).
// POINTS random columns within 7.5 million blocks of the origin (the same for every seed); a column counts for size S
// when the columns on a 3 x 3 grid of S / 2 spacing around it (5 x 5 for S >= 768) are all "slab" columns: ice caves in
// every cell from y -56 to y 16, the heights a cavern takes (the game's climate without the shift noise and the finest
// octaves). Prints "SEED cold slab s256 s512 s768 s1024": the columns at temperature <= -0.8, the slab columns, and the
// columns that are the middle of a slab square of each size.
// usage: icepatch [POINTS=400000] [THREADS=4] < seeds
// build: g++ -O3 -march=native -std=c++17 -ffp-contract=off -I ../vendor/cubiomes icepatch.cpp icebiome.o ../vendor/cub_*.o -lpthread -o icepatch
#include "icebiome.hpp"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

static bool slab(const IceBiome *b, long x, long z) {
    IceClimate c; iceClimate(b, (int) (x >> 2), (int) (z >> 2), &c, false, true); uint64_t bits[2]; iceColumn(&c, bits, 0);
    for (int qy = -14; qy <= 4; qy++) if (!((bits[(qy + 16) >> 6] >> ((qy + 16) & 63)) & 1)) return false;
    return true;
}
static bool square(const IceBiome *b, long x, long z, int size) {
    const int n = size >= 768 ? 5 : 3, step = size / (n - 1);
    for (int i = 0; i < n; i++) for (int j = 0; j < n; j++) { if (i == n / 2 && j == n / 2) continue; if (!slab(b, x + (i - n / 2) * step, z + (j - n / 2) * step)) return false; }
    return true;
}
int main(int argc, char **argv) {
    const int POINTS = argc > 1 ? atoi(argv[1]) : 400000, threads = argc > 2 ? atoi(argv[2]) : 4;
    std::vector<long> xs(POINTS), zs(POINTS); uint64_t state = 0x9E3779B97F4A7C15ULL;
    auto next = [&] { state ^= state << 13; state ^= state >> 7; state ^= state << 17; return state; };
    for (int i = 0; i < POINTS; i++) { xs[i] = (long) (next() % 14990000) - 7495000; zs[i] = (long) (next() % 14990000) - 7495000; }
    std::vector<long long> seeds; char line[512]; while (fgets(line, sizeof line, stdin)) seeds.push_back(strtoll(line, 0, 10));
    std::atomic<size_t> at{0}; std::mutex lock; std::vector<std::thread> pool;
    for (int t = 0; t < threads; t++) pool.emplace_back([&] {
        for (size_t i; (i = at.fetch_add(1)) < seeds.size();) {
            IceBiome *b = iceNew((uint64_t) seeds[i], false); long cold = 0, slabs = 0, s[4] = {0, 0, 0, 0};
            for (int k = 0; k < POINTS; k++) {
                if (iceTemperature(b, (int) (xs[k] >> 2), (int) (zs[k] >> 2)) > -8000) continue;
                cold++; if (!slab(b, xs[k], zs[k])) continue;
                slabs++;
                if (!square(b, xs[k], zs[k], 256)) continue; s[0]++;
                if (!square(b, xs[k], zs[k], 512)) continue; s[1]++;
                if (!square(b, xs[k], zs[k], 768)) continue; s[2]++;
                if (square(b, xs[k], zs[k], 1024)) s[3]++;
            }
            iceFree(b); std::lock_guard<std::mutex> hold(lock); printf("%lld %ld %ld %ld %ld %ld %ld\n", seeds[i], cold, slabs, s[0], s[1], s[2], s[3]); fflush(stdout);
        }
    });
    for (auto &t : pool) t.join();
    return 0;
}
