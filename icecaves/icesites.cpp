// icecaves/icesites.cpp: where in a world the ice caves are, from the climate alone. A lattice of points every 128
// blocks is sampled (temperature first: it rules out 97 in 100); of each GROUP x GROUP blocks of points one that is
// inside the ice caves box in temperature, humidity, continentalness and weirdness is given: a place for iceformula.
// Output: "SEED X -30 Z | t h c w"
// build: g++ -O3 -march=native -std=c++17 -ffp-contract=off -I ../vendor/cubiomes icesites.cpp icebiome.o ../vendor/cub_*.o -lpthread -o icesites
// usage: icesites SEED X0 Z0 X1 Z1 [THREADS=12] [GROUP=512]
#include "icebiome.hpp"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
int main(int argc, char **argv) {
    if (argc < 6) { fprintf(stderr, "usage: icesites SEED X0 Z0 X1 Z1 [THREADS=12]\n"); return 1; }
    const long long seed = atoll(argv[1]); const int threads = argc > 6 ? atoi(argv[6]) : 12, group = argc > 7 ? atoi(argv[7]) : 512, per = group / 128; const long x0 = atol(argv[2]) / group * group, z0 = atol(argv[3]) / group * group, x1 = atol(argv[4]), z1 = atol(argv[5]);
    IceBiome *b = iceNew((uint64_t) seed, false); const long nx = (x1 - x0) / group + 1, nz = (z1 - z0) / group + 1; std::atomic<long> next{0}, sites{0}, cold{0}; std::mutex lock;
    std::vector<std::thread> pool;
    for (int t = 0; t < threads; t++) pool.emplace_back([&] {
        std::string out; char line[160]; long mine = 0, coldPoints = 0;
        for (long j; (j = next.fetch_add(1)) < nz;) {
            for (long i = 0; i < nx; i++) {
                bool given = false;
                for (int k = 0; k < per * per && !given; k++) {
                    const long x = x0 + i * group + (k % per) * 128, z = z0 + j * group + (k / per) * 128;
                    if (iceTemperature(b, (int) (x >> 2), (int) (z >> 2)) > -8000) continue;
                    coldPoints++; IceClimate c; iceClimate(b, (int) (x >> 2), (int) (z >> 2), &c, false, false);
                    if (iceBoxDistance(&c) != 0) continue;
                    snprintf(line, sizeof line, "%lld %ld -30 %ld | %.3f %.3f %.3f %.3f\n", seed, x, z, c.t, c.h, c.c, c.w); out += line; mine++; given = true;
                }
            }
            if (out.size() > 1 << 16) { std::lock_guard<std::mutex> hold(lock); fputs(out.c_str(), stdout); out.clear(); }
        }
        std::lock_guard<std::mutex> hold(lock); fputs(out.c_str(), stdout); sites += mine; cold += coldPoints;
    });
    for (auto &t : pool) t.join();
    fprintf(stderr, "%ld groups of lattice points: %ld places inside the ice caves box (cold enough at %ld points)\n", nx * nz, (long) sites, (long) cold);
}
