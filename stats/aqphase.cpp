// The height phases of the aquifer noises of a seed, for the question whether they predict drier caverns: for seeds on
// stdin, the lattice phase at y = 20 (the middle of the cavern layer) of the two parts (A, B) of the floodedness noise
// (octave -7, sampled at 0.67 y) and of the spread noise (octave -5, 0.7142857 y).
// build: g++ -O3 -march=native -std=c++17 -I . -I vendor/cubiomes stats/aqphase.cpp vendor/cub_noise.o -o stats/aqphase
#include "cavenoise.hpp"
#include <iostream>
#include <string>
int main() {
    std::string l;
    while (std::getline(std::cin, l)) {
        if (l.empty()) continue; long long seed = std::stoll(l);
        NormalNoise flood = makeNoise((uint64_t) seed, "minecraft:aquifer_fluid_level_floodedness"), spread = makeNoise((uint64_t) seed, "minecraft:aquifer_fluid_level_spread");
        auto phase = [](const NoiseLayer &layer, double scale) { double v = 20.0 * scale * layer.frequency + layer.perlin.b; return v - std::floor(v); };
        printf("%lld %.5f %.5f %.5f %.5f\n", seed, phase(flood.layers[0], 0.67), phase(flood.layers[1], 0.67), phase(spread.layers[0], 0.7142857142857143), phase(spread.layers[1], 0.7142857142857143));
    }
}
