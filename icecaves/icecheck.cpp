// icecaves/icecheck.cpp: points to ask the real snapshot about, with this code's answer: random cells of a seed near the
// ice caves box at random heights. Output: "SEED X Y Z ice(strict) ice(with ties)" for java/BiomeProbe.
// build: g++ -O3 -march=native -std=c++17 -ffp-contract=off -I ../vendor/cubiomes icecheck.cpp icebiome.o ../vendor/cub_*.o -o icecheck
// usage: icecheck SEED [COUNT=4000] [LARGE=0]
#include "icebiome.hpp"
#include <cstdio>
#include <cstdlib>
#include <random>
int main(int argc, char **argv) {
    const long long seed = argc > 1 ? atoll(argv[1]) : 1; const int count = argc > 2 ? atoi(argv[2]) : 4000; IceBiome *b = iceNew((uint64_t) seed, argc > 3 && atoi(argv[3]));
    std::mt19937_64 rng(11); int made = 0;
    while (made < count) {
        const int qx = (int) (rng() % 4000000) - 2000000, qz = (int) (rng() % 4000000) - 2000000; IceClimate c; iceClimate(b, qx, qz, &c, true, true);
        if (iceBoxDistance(&c) > 3500LL * 3500LL && made % 5) continue;        // mostly near the box, some anywhere
        const int qy = (int) (rng() % 30) - 16;
        printf("%lld %d %d %d %d %d\n", seed, qx * 4, qy * 4, qz * 4, iceAt(&c, qy, 0), iceAt(&c, qy, 1)); made++;
    }
}
