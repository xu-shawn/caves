// prints "density NAME X Y Z value" for the named density functions at random blocks, to compare with java/NoiseProbe
#include "density_gen.hpp"
#include <cstdlib>
int main(int argc, char **argv) {
    World world(strtoull(argv[1], 0, 10)); srand(11);
    for (auto &named : GEN_NAMED) for (int i = 0; i < 60; i++) {
        int x = rand() % 200001 - 100000, y = rand() % 320 - 64, z = rand() % 200001 - 100000;
        if (i >= 40) { x = 10834544 + rand() % 400 - 200; z = -28577623 + rand() % 400 - 200; y = rand() % 180 - 54; }   // inside a big cave too
        Col col; col.clear(); P p; p.w = &world; p.x = x; p.z = z; p.col = &col; p.at(y);
        printf("density %s %d %d %d %.9g\n", named.name, x, y, z, named.function(p));
    }
}
