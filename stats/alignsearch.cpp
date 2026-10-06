// Tail probe: a complete search of the alignments of the two cave_cheese stacks for regions of low smooth noise.
// The smooth fields A and B (512-block window means of the two lowest octaves, 256-block cells, 256 x 256 cells each,
// periodic) can meet at any offset (the pair trick). For each of the 65,536 offsets: D = max(0, -0.16 - (A + B)) per
// cell, summed over boxes of 8 x 8 cells (2,048 blocks); boxes of at least MIN (default 0.9) that are the best within
// 3 cells in position and offset are kept (the best KEEP per seed, default 300), and each gives its world places.
// Prints "seed x z | boxVolume offsetX offsetZ cellX cellZ" per place (x, z: the middle of the box).
// build: g++ -O3 -march=native -std=c++17 -I . -I vendor/cubiomes stats/alignsearch.cpp vendor/cub_noise.o -lpthread -o stats/alignsearch
#include "analyticgate.hpp"
#include <atomic>
#include <cstdio>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
static const int G = 256, BOX = 8, NA = 1011, NB = 993, COMMON = 334641; static const double STEP = 65536.0 / NA, BORDER = 29990000.0; static const float LEVEL = -0.16f;
struct Found { float vol; int dx, dz, u, v; };
int main(int argc, char **argv) {
    int threads = argc > 1 ? atoi(argv[1]) : 8; const float minimum = argc > 2 ? (float) atof(argv[2]) : 0.9f; const size_t keep = argc > 3 ? atoi(argv[3]) : 300;
    std::vector<uint64_t> seeds; std::string l; while (std::getline(std::cin, l)) if (!l.empty()) seeds.push_back((uint64_t) std::stoll(l));
    std::mutex lock;
    for (uint64_t seed : seeds) {
        AnalyticGate gate; const int N = AnalyticGate::N; NormalNoise cheese = makeNoise(seed, "minecraft:cave_cheese"); float lowest; gate.value(cheese, lowest);
        std::vector<float> ga(G * G), gb2((size_t) 2 * G * 2 * G); // B laid out twice per axis: no wrap inside a row
        for (int i = 0; i < G; i++) for (int j = 0; j < G; j++) ga[i * G + j] = gate.field[0][(size_t) (2 * i) * N + 2 * j];
        for (int i = 0; i < 2 * G; i++) for (int j = 0; j < 2 * G; j++) gb2[(size_t) i * 2 * G + j] = gate.field[1][(size_t) (2 * (i & 255)) * N + 2 * (j & 255)];
        std::vector<Found> all; std::atomic<int> next(0); std::vector<std::thread> pool;
        for (int t = 0; t < threads; t++) pool.emplace_back([&] {
            std::vector<float> d((size_t) G * (G + BOX)), rows((size_t) (G + BOX) * G), box((size_t) G * G); std::vector<Found> mine;
            for (int dx; (dx = next++) < G;) for (int dz = 0; dz < G; dz++) {
                bool any = false;
                for (int u = 0; u < G; u++) { // the clipped depth, each row with its first BOX cells again at the end
                    const float *a = &ga[u * G], *b = &gb2[(size_t) (u + dx) * 2 * G + dz]; float *o = &d[(size_t) u * (G + BOX)]; float most = 0;
                    for (int v = 0; v < G; v++) { float x = LEVEL - (a[v] + b[v]); x = x > 0 ? x : 0; o[v] = x; most = most > x ? most : x; }
                    for (int v = 0; v < BOX; v++) o[G + v] = o[v];
                    any |= most > 0.08f;
                }
                if (!any) continue;
                for (int u = 0; u < G; u++) { const float *o = &d[(size_t) u * (G + BOX)]; float *r = &rows[(size_t) u * G]; float s = 0; for (int v = 0; v < BOX; v++) s += o[v]; for (int v = 0; v < G; v++) { r[v] = s; s += o[v + BOX] - o[v]; } }
                for (int u = 0; u < BOX; u++) for (int v = 0; v < G; v++) rows[(size_t) (G + u) * G + v] = rows[(size_t) u * G + v];
                for (int v = 0; v < G; v++) { float s = 0; for (int u = 0; u < BOX; u++) s += rows[(size_t) u * G + v]; for (int u = 0; u < G; u++) { box[(size_t) u * G + v] = s; s += rows[(size_t) (u + BOX) * G + v] - rows[(size_t) u * G + v]; } }
                for (int u = 0; u < G; u++) for (int v = 0; v < G; v++) if (box[(size_t) u * G + v] >= minimum) {
                    float x = box[(size_t) u * G + v]; bool top = true; // the best within 3 cells of position, for this offset
                    for (int a = -3; a <= 3 && top; a++) for (int b = -3; b <= 3; b++) if (box[(size_t) ((u + a) & 255) * G + ((v + b) & 255)] > x) { top = false; break; }
                    if (top) mine.push_back({x, dx, dz, u, v});
                }
            }
            std::lock_guard<std::mutex> guard(lock); all.insert(all.end(), mine.begin(), mine.end());
        });
        for (auto &t : pool) t.join();
        // the same region shows up at neighbouring offsets: keep the best within 3 cells of position and 3 of offset
        std::sort(all.begin(), all.end(), [](const Found &x, const Found &y) { return x.vol > y.vol; });
        std::vector<Found> kept;
        auto near = [](int a, int b) { int d = std::abs(a - b); return std::min(d, G - d) <= 3; };
        for (const Found &f : all) { bool fresh = true; for (const Found &k : kept) if (near(f.dx, k.dx) && near(f.dz, k.dz) && near(f.u, k.u) && near(f.v, k.v)) { fresh = false; break; } if (fresh) { kept.push_back(f); if (kept.size() >= keep) break; } }
        auto solve = [](int a, int b) { int d = (((b - a) / 3) % 331 + 331) % 331; return (long) a + 1011L * ((d * 276) % 331); };
        // The places. A cell offset is 256 blocks wide, but the two stacks only meet at offsets that are a multiple of 3
        // grid steps (194 blocks): per axis the two such offsets nearest to the cell offset, so up to 4 families of places.
        auto indices = [&](int cellA, int cellB, long *out) {
            int a = ((int) std::lround(cellA * (double) NA / 256) % NA + NA) % NA; double wanted = cellB * (double) NB / 256 - a; long low = (long) std::floor(wanted / 3) * 3;
            for (int k = 0; k < 2; k++) { int b = (int) (((a + low + 3 * k) % NB + NB) % NB); out[k] = solve(a, b); }
        };
        size_t places = 0;
        for (const Found &f : kept) {
            long gi[2], gj[2]; indices(f.u, (f.u + f.dx) & 255, gi); indices(f.v, (f.v + f.dz) & 255, gj);
            for (int fa = 0; fa < 2; fa++) for (int fb = 0; fb < 2; fb++)
                for (long i = gi[fa] - 2L * COMMON; i <= gi[fa] + COMMON; i += COMMON) for (long j = gj[fb] - 2L * COMMON; j <= gj[fb] + COMMON; j += COMMON) {
                    double x = i * STEP + BOX * 128.0 + 128, z = j * STEP + BOX * 128.0 + 128; // the middle of the box of 8 cells (512-block windows 256 apart) that starts at cell u
                    if (x - 2000 < -BORDER || z - 2000 < -BORDER || x + 2000 > BORDER || z + 2000 > BORDER) continue;
                    printf("%lld %.0f %.0f | %.3f %d %d %d %d\n", (long long) seed, x, z, f.vol, f.dx, f.dz, f.u, f.v); places++;
                }
        }
        fprintf(stderr, "seed %lld: %zu boxes of %.2f+ before thinning, %zu kept (best %.3f, last %.3f), %zu places\n", (long long) seed, all.size(), minimum, kept.size(), kept.empty() ? 0 : kept[0].vol, kept.empty() ? 0 : kept.back().vol, places);
    }
}
