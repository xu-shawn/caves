// prints "noise NAME X Y Z" probes and this implementation's values, to compare with java/NoiseProbe
#include "cavenoise.hpp"
#include <cstdio>
#include <cstdlib>
int main(int argc, char **argv) {
    uint64_t seed = strtoull(argv[1], 0, 10); const char *names[] = {"minecraft:cave_cheese", "minecraft:cave_layer", "minecraft:aquifer_fluid_level_floodedness", "minecraft:pillar", "minecraft:cave_entrance", "minecraft:continentalness"};
    srand(7);
    for (const char *name : names) {
        NormalNoise noise = makeNoise(seed, name);
        for (int i = 0; i < 200; i++) {
            double x = (rand() % 2000001 - 1000000) + (rand() % 1000) / 1000.0, y = (rand() % 600 - 200) + (rand() % 1000) / 1000.0, z = (rand() % 60000001 - 30000000) + (rand() % 1000) / 1000.0;
            printf("noise %s %.3f %.3f %.3f %.9g\n", name, x, y, z, noise.sample(x, y, z));
        }
    }
}
