// The native generator's terrain for a region, in CaveDump's format (bytes for z, x, y from -64 to 319: 0 solid, 1 air,
// 2 water, 3 lava), to compare with the real game's: nativedump SEED X0 Z0 SIZE_X SIZE_Z OUT [THREADS]
#include "gamegen.hpp"
#include <atomic>
#include <chrono>
#include <thread>
int main(int argc, char **argv) {
    uint64_t seed = strtoull(argv[1], 0, 10); int x0 = atoi(argv[2]), z0 = atoi(argv[3]), sx = atoi(argv[4]), sz = atoi(argv[5]), threads = argc > 7 ? atoi(argv[7]) : 8;
    World world(seed);
    std::vector<uint8_t> grid((size_t) sx * sz * 384, 1);
    std::atomic<int> next(0); std::vector<std::thread> pool; int chunksX = sx / 16, chunksZ = sz / 16;
    auto start = std::chrono::steady_clock::now();
    for (int t = 0; t < threads; t++) pool.emplace_back([&] {
        Generator generator(world); std::vector<uint8_t> codes(256 * GEN_H);
        for (int c; (c = next++) < chunksX * chunksZ;) {
            int cx = (x0 >> 4) + c % chunksX, cz = (z0 >> 4) + c / chunksX;
            generator.chunk(cx, cz, codes.data());
            for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++)
                memcpy(&grid[((size_t) ((cz << 4) + z - z0) * sx + ((cx << 4) + x - x0)) * 384], &codes[(z * 16 + x) * GEN_H], GEN_H);
        }
    });
    for (auto &t : pool) t.join();
    double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    FILE *f = fopen(argv[6], "wb"); fwrite(grid.data(), 1, grid.size(), f); fclose(f);
    printf("%d x %d columns in %.1f s (%.0f columns/s)\n", sx, sz, seconds, sx * (double) sz / seconds);
}
