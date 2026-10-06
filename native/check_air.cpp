// checks AirMaker::chunkAir (cached, with the dry shortcut) against Generator::chunk, block for block, and times both
// usage: check_air SEED X Z CHUNKS [SEED X Z CHUNKS ...]   (a square of CHUNKS x CHUNKS chunks from the chunk of X Z)
#include "gamegen.hpp"
#include <chrono>
static double now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
int main(int argc, char **argv) {
    long totalDifferent = 0;
    for (int a = 1; a + 3 < argc; a += 4) {
        uint64_t seed = strtoull(argv[a], 0, 10); int x = atoi(argv[a + 1]) >> 4, z = atoi(argv[a + 2]) >> 4, n = atoi(argv[a + 3]);
        World world(seed); Generator g(world); AirMaker maker(world);
        std::vector<uint8_t> codes(256 * GEN_H); std::vector<uint64_t> air(256 * GEN_WORDS);
        long different = 0, airBlocks = 0, fluid = 0; double slow = 0, fast = 0;
        for (int dz = 0; dz < n; dz++) for (int dx = 0; dx < n; dx++) {
            double t0 = now(); g.chunk(x + dx, z + dz, codes.data()); double t1 = now(); maker.chunkAir(x + dx, z + dz, air.data()); double t2 = now();
            slow += t1 - t0; fast += t2 - t1;
            for (int column = 0; column < 256; column++) for (int y = 0; y < GEN_H; y++) {
                bool isAir = air[column * GEN_WORDS + (y >> 6)] >> (y & 63) & 1;
                different += isAir != (codes[column * GEN_H + y] == AIR); airBlocks += isAir; fluid += codes[column * GEN_H + y] >= WATER;
            }
        }
        long blocks = (long) n * n * 256 * GEN_H; totalDifferent += different;
        printf("seed %llu at %d %d, %d chunks: %ld different blocks of %ld (air %.1f%%, fluid %.1f%%); chunk() %.2f ms, chunkAir() %.2f ms per chunk\n", (unsigned long long) seed, x << 4, z << 4, n * n, different, blocks,
               100.0 * airBlocks / blocks, 100.0 * fluid / blocks, slow * 1000 / (n * n), fast * 1000 / (n * n));
    }
    return totalDifferent != 0;
}
