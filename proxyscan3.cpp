// Stage 1 of the cave search: where could a giant dry cheese cave be?
//
// Formula per point: open if clamp(0.27 + cave_cheese(x, 2y/3, z), -1, 1) + 4 cave_layer(x, 8y, z)^2 < 0 (the game's
// cheese-cave term deep underground, without interpolation or the other cave types), dry if the aquifer floodedness noise
// there is at most 0.4 (above that the game puts water in), and only points at least 32 blocks under the surface count.
// The surface estimate is where the game's depth is 0 (128 x cubiomes' depth at y = 0), one per 64 x 64 blocks.
// Two levels over 128-block tiles:
//   coarse: 2 x 2 columns a tile, heights y -40 .. 200 step 16. A tile goes on only if the count of its 3 x 3 tile
//           neighbourhood is at least T1.
//   fine:   8 x 8 columns a tile, heights y -48 .. 200 step 8.
// Then 512-block windows (4 x 4 tiles, stepping 64 blocks) are summed and the windows with at least T2 open-and-dry fine
// points that are the best within 256 blocks are printed: "seed x z dry open surface" (x, z = window centre; points, 2048 blocks each; surface estimate there).
//
// build: g++ -O3 -march=native -std=c++17 -I vendor/cubiomes proxyscan3.cpp vendor/cub_*.o -lpthread -o proxyscan3
// usage: proxyscan3 SEED X0 Z0 SIZE T1 T2 [THREADS=8] [MAPS]    (X0, Z0, SIZE in blocks, multiples of 128; with MAPS the
//        coarse and fine dry counts are written to MAPS.coarse and MAPS.fine, a byte per column)
#include "cavenoise.hpp"
extern "C" {
#include "biomenoise.h"
}
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>
int main(int argc, char **argv) {
    if (argc < 7) { fprintf(stderr, "usage: proxyscan3 SEED X0 Z0 SIZE T1 T2 [THREADS] [MAPS]\n"); return 1; }
    const uint64_t seed = strtoull(argv[1], 0, 10); const long x0 = atol(argv[2]), z0 = atol(argv[3]); const int size = atoi(argv[4]), t1 = atoi(argv[5]), t2 = atoi(argv[6]);
    const int threads = argc > 7 ? atoi(argv[7]) : 8, tiles = size / 128, nc = 2 * tiles, nf = 8 * tiles;
    const NormalNoise cheese = makeNoise(seed, "minecraft:cave_cheese"), layer = makeNoise(seed, "minecraft:cave_layer"), flood = makeNoise(seed, "minecraft:aquifer_fluid_level_floodedness");
    BiomeNoise biomeNoise; initBiomeNoise(&biomeNoise, MC_26_3); setBiomeSeed(&biomeNoise, seed, 0);
    const int MARGIN = 32;
    auto column = [&](double x, double z, int yFirst, int yStep, int levels, float surface, int *open) {
        int dry = 0; *open = 0;
        for (int k = 0; k < levels; k++) {
            int y = yFirst + k * yStep;
            if (y > surface - MARGIN) break;
            float c = cheese.sample(x, y * 0.6666666666666666, z) + 0.27f;
            if (c >= 0) continue;
            if (c < -1) c = -1;
            float l = layer.sample(x, y * 8.0, z);
            if (c + 4 * l * l >= 0) continue;
            (*open)++;
            dry += flood.sample(x, y * 0.67, z) <= 0.4f;
        }
        return dry;
    };
    auto parallel = [&](int n, auto body) {
        std::atomic<int> next(0); std::vector<std::thread> pool;
        for (int t = 0; t < threads; t++) pool.emplace_back([&] { for (int i; (i = next++) < n;) body(i); });
        for (auto &t : pool) t.join();
    };
    // coarse
    std::vector<uint8_t> coarse((size_t) nc * nc); std::vector<float> surface((size_t) nc * nc);
    parallel(nc, [&](int row) {
        for (int col = 0; col < nc; col++) {
            double x = x0 + 64.0 * col + 32, z = z0 + 64.0 * row + 32; int64_t np[6]; int open;
            sampleBiomeNoise(&biomeNoise, np, (int) floor(x / 4), 0, (int) floor(z / 4), NULL, SAMPLE_NO_BIOME);
            surface[(size_t) row * nc + col] = 128.0f * np[4] / 10000.0f;
            coarse[(size_t) row * nc + col] = (uint8_t) column(x, z, -40, 16, 16, surface[(size_t) row * nc + col], &open);
        }
    });
    std::vector<int> tileSum((size_t) tiles * tiles);
    for (int tz = 0; tz < tiles; tz++) for (int tx = 0; tx < tiles; tx++)
        tileSum[(size_t) tz * tiles + tx] = coarse[(size_t) (2 * tz) * nc + 2 * tx] + coarse[(size_t) (2 * tz) * nc + 2 * tx + 1] + coarse[(size_t) (2 * tz + 1) * nc + 2 * tx] + coarse[(size_t) (2 * tz + 1) * nc + 2 * tx + 1];
    std::vector<uint8_t> pass((size_t) tiles * tiles); long passed = 0;
    for (int tz = 0; tz < tiles; tz++) for (int tx = 0; tx < tiles; tx++) {
        int sum = 0;
        for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) {
            int a = tz + dz, b = tx + dx;
            if (a >= 0 && b >= 0 && a < tiles && b < tiles) sum += tileSum[(size_t) a * tiles + b];
        }
        passed += pass[(size_t) tz * tiles + tx] = sum >= t1;
    }
    // fine, for the tiles that passed
    std::vector<uint8_t> fineDry((size_t) nf * nf, 0), fineOpen((size_t) nf * nf, 0);
    parallel(tiles * tiles, [&](int tile) {
        if (!pass[tile]) return;
        int tz = tile / tiles, tx = tile % tiles;
        for (int r = 0; r < 8; r++) for (int c = 0; c < 8; c++) {
            int row = 8 * tz + r, col = 8 * tx + c, open;
            fineDry[(size_t) row * nf + col] = (uint8_t) column(x0 + 16.0 * col + 8, z0 + 16.0 * row + 8, -48, 8, 32, surface[(size_t) (row / 4) * nc + col / 4], &open);
            fineOpen[(size_t) row * nf + col] = (uint8_t) open;
        }
    });
    // 512-block windows (32 x 32 fine columns) every 64 blocks
    const int WINDOW = 32, nw = (nf - WINDOW) / 4 + 1;
    std::vector<int> prefixDry((size_t) (nf + 1) * (nf + 1), 0), prefixOpen((size_t) (nf + 1) * (nf + 1), 0);
    for (int r = 0; r < nf; r++) for (int c = 0; c < nf; c++) {
        size_t i = (size_t) (r + 1) * (nf + 1) + c + 1;
        prefixDry[i] = fineDry[(size_t) r * nf + c] + prefixDry[i - 1] + prefixDry[i - (nf + 1)] - prefixDry[i - (nf + 1) - 1];
        prefixOpen[i] = fineOpen[(size_t) r * nf + c] + prefixOpen[i - 1] + prefixOpen[i - (nf + 1)] - prefixOpen[i - (nf + 1) - 1];
    }
    auto boxSum = [&](const std::vector<int> &prefix, int r, int c) { return prefix[(size_t) (r + WINDOW) * (nf + 1) + c + WINDOW] - prefix[(size_t) r * (nf + 1) + c + WINDOW] - prefix[(size_t) (r + WINDOW) * (nf + 1) + c] + prefix[(size_t) r * (nf + 1) + c]; };
    std::vector<int> window((size_t) nw * nw);
    for (int r = 0; r < nw; r++) for (int c = 0; c < nw; c++) window[(size_t) r * nw + c] = boxSum(prefixDry, 4 * r, 4 * c);
    long printed = 0;
    for (int r = 0; r < nw; r++) for (int c = 0; c < nw; c++) {
        int value = window[(size_t) r * nw + c];
        if (value < t2) continue;
        bool best = true;
        for (int dr = -4; dr <= 4 && best; dr++) for (int dc = -4; dc <= 4; dc++) {
            int a = r + dr, b = c + dc;
            if ((dr || dc) && a >= 0 && b >= 0 && a < nw && b < nw) {
                int other = window[(size_t) a * nw + b];
                if (other > value || (other == value && (dr < 0 || (dr == 0 && dc < 0)))) { best = false; break; }
            }
        }
        if (best) { printf("%lld %ld %ld %d %d %.0f\n", (long long) seed, x0 + 64L * c + 256, z0 + 64L * r + 256, value, boxSum(prefixOpen, 4 * r, 4 * c), surface[(size_t) (r + 4) * nc + c + 4]); printed++; }
    }
    fprintf(stderr, "seed %lld, %ld..%ld x %ld..%ld: %ld of %d tiles to the fine level (%.3f), %ld windows printed\n", (long long) seed, x0, x0 + size, z0, z0 + size, passed, tiles * tiles,
            (double) passed / (tiles * tiles), printed);
    if (argc > 8) {
        std::string base = argv[8];
        FILE *f = fopen((base + ".coarse").c_str(), "wb"); fwrite(coarse.data(), 1, coarse.size(), f); fclose(f);
        f = fopen((base + ".fine").c_str(), "wb"); fwrite(fineDry.data(), 1, fineDry.size(), f); fclose(f);
    }
}
