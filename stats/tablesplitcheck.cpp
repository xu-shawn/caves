// tableSeedOctave() (one octave at a time) against tableSeed() and against the tables made from the full noise: the same bytes.
// build: g++ -O2 -march=native -std=c++17 -I . -I vendor/cubiomes stats/tablesplitcheck.cpp vendor/cub_noise.o -o stats/tablesplitcheck
#include "tableseed.hpp"
#include <cstring>
int main() {
    uint64_t x = 88172645463325252ULL; int bad = 0;
    for (int i = 0; i < 2000; i++) {
        x ^= x << 13; x ^= x >> 7; x ^= x << 17; TableSeed a, b; memset(&a, 0, sizeof a); memset(&b, 0, sizeof b);
        tableSeed(x >> 14, a); tableSeedOctave(x >> 14, 0, b); tableSeedOctave(x >> 14, 1, b);
        TableSeed c; memset(&c, 0, sizeof c); tableSeed(makeNoise(x >> 14, "minecraft:cave_cheese"), c);
        bad += memcmp(&a, &b, sizeof a) != 0; bad += memcmp(&a, &c, sizeof a) != 0;
    }
    printf("%d of 4000 comparisons differ\n", bad);
}
