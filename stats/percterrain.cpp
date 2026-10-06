// Tail probe: the percolation volume of percgate.hpp with the terrain of each world place of an alignment.
// For the 24 best pairs of spots (by the terrain-free volume, best offset each) the places follow from the pair trick
// (the scanner's grid of 65536/1011 blocks; A window index a, B index b with b - a a multiple of 3; g = a + 1011 k).
// At each place every cell of the connected region gets the scanner's rough surface estimate, and its share of the
// volume counts by the levels (y = -40 + 16 k) that lie 48 blocks under that surface, of 12.
// Prints per seed: gate512, wide, the terrain-free volume (mean of the 8 best pairs), then with terrain: the best
// place's volume, the 3rd best, the 8th best, and the number of places at 0.3 or more.
// build: g++ -O3 -march=native -std=c++17 -I . -I vendor/cubiomes stats/percterrain.cpp vendor/cub_*.o -lpthread -o stats/percterrain
#include "percgate.hpp"
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
static const int NA = 1011, NB = 993, COMMON = 334641; static const double STEP = 65536.0 / NA, BORDER = 29990000.0;
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
struct Best { float vol; int a, b, shift; };
int main(int argc, char **argv) {
    int threads = argc > 1 ? atoi(argv[1]) : 8;
    std::vector<uint64_t> seeds; std::string l; while (std::getline(std::cin, l)) if (!l.empty()) seeds.push_back((uint64_t) std::stoll(l));
    std::atomic<size_t> next(0); std::mutex lock; std::vector<std::thread> pool;
    for (int t = 0; t < threads; t++) pool.emplace_back([&] {
        AnalyticGate gate; Percolation perc; BiomeNoise bn; initBiomeNoise(&bn, MC_26_3); const int SIDE = Percolation::SIDE, CELLS = Percolation::CELLS, SHIFTS = Percolation::SHIFTS; const float LEVEL = Percolation::LEVEL;
        for (size_t k; (k = next++) < seeds.size();) {
            NormalNoise cheese = makeNoise(seeds[k], "minecraft:cave_cheese"); float lowest, value = gate.value(cheese, lowest), wide = gate.wideValue(), free8 = perc.volume(gate);
            setBiomeSeed(&bn, seeds[k], 0);
            // the best offset of every pair of spots again, with the cells of its region
            std::vector<Best> pairs;
            for (size_t a = 0; a < perc.sa.size(); a++) for (size_t b = 0; b < perc.sb.size(); b++) {
                Best best = {0, (int) a, (int) b, 0}; const float *pa = &perc.patchA[a * CELLS];
                for (int shift = 0; shift < SHIFTS; shift++) {
                    const float *pb = &perc.patchB[(b * SHIFTS + shift) * CELLS]; uint16_t open[16], in[16] = {0}; float vol = 0;
                    for (int u = 0; u < SIDE; u++) { uint16_t m = 0; for (int v = 0; v < SIDE; v++) if (pa[u * SIDE + v] + pb[u * SIDE + v] < LEVEL) m |= (uint16_t) (1u << v); open[u] = m; }
                    in[5] = open[5] & 0x60; in[6] = open[6] & 0x60; if (!(in[5] | in[6])) continue;
                    for (bool grew = true; grew;) { grew = false; for (int u = 0; u < SIDE; u++) { uint16_t m = in[u] | (uint16_t) (in[u] << 1) | (in[u] >> 1); if (u > 0) m |= in[u - 1]; if (u + 1 < SIDE) m |= in[u + 1]; m &= open[u]; if (m != in[u]) { in[u] = m; grew = true; } } }
                    for (int u = 0; u < SIDE; u++) for (int v = 0; v < SIDE; v++) if (in[u] >> v & 1) vol += LEVEL - (pa[u * SIDE + v] + pb[u * SIDE + v]);
                    if (vol > best.vol) { best.vol = vol; best.shift = shift; }
                }
                pairs.push_back(best);
            }
            std::partial_sort(pairs.begin(), pairs.begin() + 24, pairs.end(), [](const Best &x, const Best &y) { return x.vol > y.vol; });
            std::vector<float> placeVol;
            for (int q = 0; q < 24; q++) {
                const Best &p = pairs[q]; const Percolation::Spot &A = perc.sa[p.a], &B = perc.sb[p.b]; int dx = p.shift / 5 - 2, dz = p.shift % 5 - 2;
                const float *pa = &perc.patchA[p.a * CELLS], *pb = &perc.patchB[(p.b * SHIFTS + p.shift) * CELLS]; uint16_t open[16], in[16] = {0};
                for (int u = 0; u < SIDE; u++) { uint16_t m = 0; for (int v = 0; v < SIDE; v++) if (pa[u * SIDE + v] + pb[u * SIDE + v] < LEVEL) m |= (uint16_t) (1u << v); open[u] = m; }
                in[5] = open[5] & 0x60; in[6] = open[6] & 0x60;
                for (bool grew = true; grew;) { grew = false; for (int u = 0; u < SIDE; u++) { uint16_t m = in[u] | (uint16_t) (in[u] << 1) | (in[u] >> 1); if (u > 0) m |= in[u - 1]; if (u + 1 < SIDE) m |= in[u + 1]; m &= open[u]; if (m != in[u]) { in[u] = m; grew = true; } } }
                // the places: scanner grid indices of the A cell (i, j) and the B cell that lies on it
                auto solve = [](int a, int b) { int d = (((b - a) / 3) % 331 + 331) % 331; return (long) a + 1011L * ((d * 276) % 331); };
                auto index = [&](int cellA, int cellB) { int a = (int) std::lround(cellA * (double) NA / 256) % NA, b = (int) std::lround(cellB * (double) NB / 256) % NB; a = (a + NA) % NA; b = (b + NB) % NB; while (((b - a) % 3 + 3) % 3) b = (b + 1) % NB; return solve(a, b); };
                long gi0 = index(A.i, (B.i + dx) & 255), gj0 = index(A.j, (B.j + dz) & 255);
                for (long gi = gi0 - 2L * COMMON; gi <= gi0 + COMMON; gi += COMMON) for (long gj = gj0 - 2L * COMMON; gj <= gj0 + COMMON; gj += COMMON) {
                    double x0 = gi * STEP, z0 = gj * STEP; // the world position of the start of A's window (i, j)
                    if (x0 - 1200 < -BORDER || z0 - 1200 < -BORDER || x0 + 2200 > BORDER || z0 + 2200 > BORDER) continue;
                    float vol = 0;
                    for (int u = 0; u < SIDE; u++) for (int v = 0; v < SIDE; v++) if (in[u] >> v & 1) {
                        double x = x0 + (u - 4) * 256.0 + 256, z = z0 + (v - 4) * 256.0 + 256; double h = surfaceEstimate(&bn, (int) floor(x / 4), (int) floor(z / 4), 3, 2, 2);
                        int levels = (int) std::floor((h - 48 + 40) / 16) + 1; levels = std::max(0, std::min(12, levels));
                        vol += (LEVEL - (pa[u * SIDE + v] + pb[u * SIDE + v])) * levels / 12.0f;
                    }
                    placeVol.push_back(vol);
                }
            }
            std::sort(placeVol.begin(), placeVol.end(), std::greater<float>()); int good = 0; for (float v : placeVol) good += v >= 0.3f;
            auto at = [&](size_t i) { return i < placeVol.size() ? placeVol[i] : 0.0f; };
            std::lock_guard<std::mutex> guard(lock);
            printf("%lld %.4f %.4f %.4f %.4f %.4f %.4f %d %zu\n", (long long) seeds[k], value, wide, free8, at(0), at(2), at(7), good, placeVol.size());
        }
    });
    for (auto &t : pool) t.join();
}
