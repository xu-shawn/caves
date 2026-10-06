// The three coarse looks of seedgate10 for seeds on stdin ("seed ..."): prints "seed low8y lowBothY lowBoth | rest", to see how
// far known seeds are from the cuts (-0.1748, -0.202, -0.2113).
// build: g++ -O3 -march=native -std=c++17 -I . -I vendor/cubiomes stats/lookcheck.cpp vendor/cub_noise.o -lpthread -o stats/lookcheck
#include "cpuprelook.hpp"
#include <cstdio>
#include <iostream>
#include <memory>
#include <string>
int main() {
    std::unique_ptr<CoarseLook> look(new CoarseLook); std::string line;
    while (std::getline(std::cin, line)) {
        long long seed; int used = 0; if (sscanf(line.c_str(), "%lld%n", &seed, &used) < 1) continue;
        TableSeed ts; tableSeed((uint64_t) seed, ts); float a = look->low8y(ts), b = look->lowBothY(ts); look->low8(ts); float c = look->lowBoth(ts);
        printf("%lld %.4f %.4f %.4f |%s\n", seed, a, b, c, line.c_str() + used);
    }
}
