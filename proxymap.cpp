// The cheese-cave formula without terrain, aquifers or interpolation, on a grid: for every STRIDE-th column of a region,
// the number of sampled heights where clamp(0.27 + cave_cheese, -1, 1) + 4 cave_layer^2 < 0, per height band.
// usage: proxymap SEED X0 Z0 SIZE_X SIZE_Z STRIDE Y_MIN Y_MAX Y_STEP > out   (text: one row per z, "count" per column;
// with BANDS=1 one line per column: the 0/1 flags of every height)
#include "cavenoise.hpp"
#include <cstdio>
#include <cstdlib>
int main(int argc, char **argv) {
    uint64_t seed = strtoull(argv[1], 0, 10); int x0 = atoi(argv[2]), z0 = atoi(argv[3]), sx = atoi(argv[4]), sz = atoi(argv[5]), stride = atoi(argv[6]), y0 = atoi(argv[7]), y1 = atoi(argv[8]), ys = atoi(argv[9]);
    bool bands = getenv("BANDS") != NULL;
    NormalNoise cheese = makeNoise(seed, "minecraft:cave_cheese"), layer = makeNoise(seed, "minecraft:cave_layer");
    for (int z = z0; z < z0 + sz; z += stride) {
        for (int x = x0; x < x0 + sx; x += stride) {
            int count = 0;
            for (int y = y0; y <= y1; y += ys) {
                float c = cheese.sample(x, y * 0.6666666666666666, z) + 0.27f; c = c < -1 ? -1 : c > 1 ? 1 : c;
                float l = layer.sample(x, y * 8.0, z);
                bool open = c + 4 * l * l < 0;
                count += open;
                if (bands) putchar(open ? '1' : '0');
            }
            if (bands) putchar('\n'); else printf("%d ", count);
        }
        if (!bands) putchar('\n');
    }
}
