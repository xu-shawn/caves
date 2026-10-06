// Tail probe: a complete search of the alignments of the two cave_cheese stacks for regions of low smooth noise.
// The smooth fields A and B (512-block window means of the two lowest octaves, 256-block cells, 256 x 256 cells each,
// periodic) can meet at any offset (the pair trick). For each of the 65,536 offsets: D = max(0, -0.16 - (A + B)) per
// cell, summed over boxes of 8 x 8 cells (2,048 blocks); boxes of at least MIN (default 0.9) that are the best within
// 3 cells in position and offset are kept (the best KEEP per seed, default 300), and each gives its world places.
// Prints "seed x z | boxVolume offsetX offsetZ cellX cellZ" per place (x, z: the middle of the box).
// alignplaces (this file): no thinning across offsets (every offset with a box of MIN or more is an alignment of its
// own, with its own places), and each place gets the terrain: the box volume with every cell counted by the levels
// (y = -40 + 16 k, of 12) that lie 48 blocks under the scanner's rough surface estimate there. Prints the KEEP places
// of a seed with the most terrain-weighted volume, 600 blocks apart at least: "seed x z | terrainVolume boxVolume".
// build: g++ -O3 -march=native -std=c++17 -I . -I vendor/cubiomes stats/alignplaces.cpp vendor/cub_*.o -lpthread -o stats/alignplaces
#include "analyticgate.hpp"
extern "C" {
#include "biomenoise.h"
float getSpline(const Spline *sp, const float *vals);
}
#include <atomic>
#include <cstdio>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
static const int G = 256, BOX = 8, NA = 1011, NB = 993, COMMON = 334641; static const double STEP = 65536.0 / NA, BORDER = 29990000.0; static const float LEVEL = -0.16f;
struct Found { float vol; int dx, dz, u, v; };
static inline double firstOctaves(const DoublePerlinNoise *n, double x, double z, int k) {
    const double f = 337.0 / 331.0; double v = 0;
    for (int i = 0; i < k && i < n->octA.octcnt; i++) { const PerlinNoise *p = n->octA.octaves + i; v += p->amplitude * fastPerlin(p, x * p->lacunarity, 0, z * p->lacunarity); }
    for (int i = 0; i < k && i < n->octB.octcnt; i++) { const PerlinNoise *p = n->octB.octaves + i; v += p->amplitude * fastPerlin(p, x * f * p->lacunarity, 0, z * f * p->lacunarity); }
    return v * n->amplitude;
}
static inline double surfaceEstimate(const BiomeNoise *bn, int qx, int qz, int kc, int ke, int kw) {
    float c = (float) firstOctaves(&bn->climate[NP_CONTINENTALNESS], qx, qz, kc), e = (float) firstOctaves(&bn->climate[NP_EROSION], qx, qz, ke), w = (float) firstOctaves(&bn->climate[NP_WEIRDNESS], qx, qz, kw);
    float p[] = {c, e, -3.0F * (fabsf(fabsf(w) - 0.6666667F) - 0.33333334F), w};
    return 128.0 * (1.0 - 83.0 / 160.0 + getSpline(bn->sp, p) + 0.015F);
}
struct Place { float tv, vol; double x, z; };
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
        std::sort(all.begin(), all.end(), [](const Found &x, const Found &y) { return x.vol > y.vol; });
        std::vector<Found> &kept = all; // every offset is its own alignment
        auto solve = [](int a, int b) { int d = (((b - a) / 3) % 331 + 331) % 331; return (long) a + 1011L * ((d * 276) % 331); };
        // The places. A cell offset is 256 blocks wide, but the two stacks only meet at offsets that are a multiple of 3
        // grid steps (194 blocks): per axis the two such offsets nearest to the cell offset, so up to 4 families of places.
        auto indices = [&](int cellA, int cellB, long *out) {
            int a = ((int) std::lround(cellA * (double) NA / 256) % NA + NA) % NA; double wanted = cellB * (double) NB / 256 - a; long low = (long) std::floor(wanted / 3) * 3;
            for (int k = 0; k < 2; k++) { int b = (int) (((a + low + 3 * k) % NB + NB) % NB); out[k] = solve(a, b); }
        };
        // every place of every alignment, with its terrain (threads over the alignments)
        std::vector<Place> found; std::atomic<size_t> nextFound(0); std::vector<std::thread> pool2;
        for (int t = 0; t < threads; t++) pool2.emplace_back([&] {
            BiomeNoise bn; initBiomeNoise(&bn, MC_26_3); setBiomeSeed(&bn, seed, 0); std::vector<Place> mine;
            for (size_t k; (k = nextFound++) < kept.size();) {
                const Found &f = kept[k]; long gi[2], gj[2]; indices(f.u, (f.u + f.dx) & 255, gi); indices(f.v, (f.v + f.dz) & 255, gj); float depth[BOX][BOX];
                for (int i = 0; i < BOX; i++) for (int j = 0; j < BOX; j++) { float x = LEVEL - (ga[((f.u + i) & 255) * G + ((f.v + j) & 255)] + gb2[(size_t) (((f.u + i) & 255) + f.dx) * 2 * G + ((f.v + j) & 255) + f.dz]); depth[i][j] = x > 0 ? x : 0; }
                for (int fa = 0; fa < 2; fa++) for (int fb = 0; fb < 2; fb++)
                    for (long i = gi[fa] - 2L * COMMON; i <= gi[fa] + COMMON; i += COMMON) for (long j = gj[fb] - 2L * COMMON; j <= gj[fb] + COMMON; j += COMMON) {
                        double x0 = i * STEP, z0 = j * STEP, x = x0 + BOX * 128.0 + 128, z = z0 + BOX * 128.0 + 128;
                        if (x - 2000 < -BORDER || z - 2000 < -BORDER || x + 2000 > BORDER || z + 2000 > BORDER) continue;
                        float tv = 0;
                        for (int a = 0; a < BOX; a++) for (int b = 0; b < BOX; b++) if (depth[a][b] > 0) {
                            double h = surfaceEstimate(&bn, (int) floor((x0 + a * 256.0 + 256) / 4), (int) floor((z0 + b * 256.0 + 256) / 4), 3, 2, 2); int levels = (int) std::floor((h - 48 + 40) / 16) + 1; levels = std::max(0, std::min(12, levels));
                            tv += depth[a][b] * levels / 12.0f;
                        }
                        mine.push_back({tv, f.vol, x, z});
                    }
            }
            std::lock_guard<std::mutex> guard(lock); found.insert(found.end(), mine.begin(), mine.end());
        });
        for (auto &t : pool2) t.join();
        std::sort(found.begin(), found.end(), [](const Place &x, const Place &y) { return x.tv > y.tv; });
        std::vector<Place> top; size_t places = found.size();
        for (const Place &q : found) { bool fresh = true; for (const Place &k : top) if (std::fabs(q.x - k.x) < 600 && std::fabs(q.z - k.z) < 600) { fresh = false; break; } if (fresh) { top.push_back(q); if (top.size() >= keep) break; } }
        for (const Place &q : top) printf("%lld %.0f %.0f | %.3f %.3f\n", (long long) seed, q.x, q.z, q.tv, q.vol);
        fprintf(stderr, "seed %lld: %zu boxes of %.2f+ before thinning, %zu kept (best %.3f, last %.3f), %zu places\n", (long long) seed, all.size(), minimum, kept.size(), kept.empty() ? 0 : kept[0].vol, kept.empty() ? 0 : kept.back().vol, places);
    }
}
