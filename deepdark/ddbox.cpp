// Deep dark in the bounding boxes of measured caverns, on cubiomes (ddbiome.hpp). No terrain: every 4 x 4 x 4 biome cell
// that holds a block of the box x0..x1, y0..y1, z0..z1 is looked up.
//
// build: g++ -O2 -std=c++17 -ffp-contract=off -c ddbiome.cpp -I ../vendor/cubiomes -o ddbiome.o
//        g++ -O2 -std=c++17 ddbox.cpp ddbiome.o ../vendor/cub_*.o -lpthread -o ddbox
// usage: ddbox box [THREADS=1] < lines "RANK SEED TX TY TZ VOLUME CORE x0 x1 y0 y1 z0 z1 ..."   (ranked.txt)
//          prints "RANK SEED TX TY TZ VOLUME | cells N dd N share S | cols N ddcols N | ddtop Y | notprefix N | bottom N"
//          cells: biome cells of the box; dd: the deep dark ones; ddcols: columns of cells with any; ddtop: the highest
//          block y of a deep dark cell of the box (- if none); notprefix: columns whose deep dark cells are not exactly
//          the lowest ones of the column (0 = "deep dark is a layer at the bottom" holds); bottom: columns whose lowest
//          cell is deep dark.
//        ddbox points < lines "SEED X Y Z" (blocks)
//          prints "SEED X Y Z cell BIOME zoom BIOME": the biome of the block's own cell and the biome the game's
//          BiomeManager zoom gives the block (to compare with the real game: java/BiomeProbe).
//        ddbox selftest SEED N      N random cells: the split lookup against cubiomes' sampleBiomeNoise
// DD_BUMP=n in the environment (box and points): n units of 1/10000 added to every cell's depth, see ddbiome.hpp
// (DD_BUMP=2: deep dark or a tie the game may turn into deep dark; DD_BUMP=-2: deep dark with no tie in play).
#include "ddbiome.hpp"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

int main(int argc, char **argv) {
    std::string mode = argc > 1 ? argv[1] : "";
    if (mode == "selftest") {
        uint64_t seed = strtoull(argv[2], NULL, 10); long n = atol(argv[3]), bad = 0, dd = 0; DDBiome *b = ddNew(seed);
        uint64_t s = 88172645463325252ULL; auto rnd = [&] { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; };
        for (long i = 0; i < n; i++) {
            int qx = (int) (rnd() % 15000000) - 7500000, qz = (int) (rnd() % 15000000) - 7500000, qy = (int) (rnd() % 80) - 16;
            DDClimate cc; ddColumn(b, qx, qz, &cc); int a = ddBiomeAt(&cc, qy), d = ddBiomeDirect(b, qx, qy, qz);
            if (a != d) bad++;
            if (d == DD_DEEP_DARK) dd++;
        }
        printf("selftest seed %llu cells %ld deep_dark %ld mismatches %ld\n", (unsigned long long) seed, n, dd, bad);
        return bad != 0;
    }
    if (getenv("DD_BUMP")) ddSetDepthBump(atoi(getenv("DD_BUMP")));
    if (mode == "points") {
        std::string line; uint64_t cached = 0; DDBiome *b = NULL;
        while (std::getline(std::cin, line)) {
            long long seed; int x, y, z;
            if (sscanf(line.c_str(), "%lld %d %d %d", &seed, &x, &y, &z) < 4) continue;
            if (!b || cached != (uint64_t) seed) { if (b) ddFree(b); b = ddNew((uint64_t) seed); cached = (uint64_t) seed; }
            DDClimate cc; ddColumn(b, x >> 2, z >> 2, &cc); int cell = ddBiomeAt(&cc, y >> 2);
            int qx, qy, qz; ddVoronoi(b, x, y, z, &qx, &qy, &qz);
            ddColumn(b, qx, qz, &cc); int zoom = ddBiomeAt(&cc, qy);
            printf("%lld %d %d %d cell %s zoom %s\n", seed, x, y, z, ddName(cell), ddName(zoom));
        }
        return 0;
    }
    if (mode != "box") { fprintf(stderr, "usage: ddbox box [THREADS] | points | selftest SEED N\n"); return 2; }
    int threads = argc > 2 ? atoi(argv[2]) : 1;
    std::vector<std::string> lines; std::string line;
    while (std::getline(std::cin, line)) lines.push_back(line);
    std::atomic<size_t> next(0); std::mutex print; std::vector<std::thread> pool;
    for (int t = 0; t < std::max(1, std::min<int>(threads, (int) lines.size())); t++) pool.emplace_back([&] {
        for (size_t k; (k = next++) < lines.size();) {
            long long rank, seed, volume, core; int tx, ty, tz, x0, x1, y0, y1, z0, z1; char buf[400]; buf[0] = 0;
            if (sscanf(lines[k].c_str(), "%lld %lld %d %d %d %lld %lld %d %d %d %d %d %d", &rank, &seed, &tx, &ty, &tz, &volume, &core, &x0, &x1, &y0, &y1, &z0, &z1) == 13) {
                DDBiome *b = ddNew((uint64_t) seed);
                long cells = 0, dd = 0, cols = 0, ddcols = 0, notprefix = 0, bottom = 0; int ddtop = -1000;
                for (int qz = z0 >> 2; qz <= z1 >> 2; qz++) for (int qx = x0 >> 2; qx <= x1 >> 2; qx++) {
                    DDClimate cc; ddColumn(b, qx, qz, &cc); int n = 0, prefix = 0; bool run = true;
                    for (int qy = y0 >> 2; qy <= y1 >> 2; qy++) {
                        bool is = ddBiomeAt(&cc, qy) == DD_DEEP_DARK;
                        if (is) { n++; if (run) prefix++; ddtop = std::max(ddtop, qy * 4 + 3); } else run = false;
                    }
                    cells += (y1 >> 2) - (y0 >> 2) + 1; dd += n; cols++;
                    if (n) ddcols++;
                    if (n != prefix) notprefix++;
                    if (prefix) bottom++;
                }
                ddFree(b);
                char top[16]; if (dd) snprintf(top, sizeof top, "%d", ddtop); else strcpy(top, "-");
                snprintf(buf, sizeof buf, "%lld %lld %d %d %d %lld | cells %ld dd %ld share %.5f | cols %ld ddcols %ld | ddtop %s | notprefix %ld | bottom %ld",
                         rank, seed, tx, ty, tz, volume, cells, dd, (double) dd / cells, cols, ddcols, top, notprefix, bottom);
            }
            if (!buf[0]) continue;
            std::lock_guard<std::mutex> guard(print); // as each is done: sort by rank afterwards
            puts(buf); fflush(stdout);
        }
    });
    for (auto &t : pool) t.join();
}
