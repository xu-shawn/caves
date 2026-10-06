// Tail probe, after the remark that the giants look like percolation clusters: is a seed's best *connected* region of
// low smooth noise, of any shape, a better sign of giants than the window means of the gate?
// The smooth field: the 512-block window means of the two lowest cave_cheese octaves (analyticgate.hpp), per stack, on a
// grid of 256 blocks. For an alignment of the two stacks (any offset between them exists somewhere in the world: the
// pair trick), S = A + B over a 12 x 12 neighbourhood (3,072 blocks). For a threshold t the cells with S < t that hang
// together with the centre (4 neighbours) are the candidate cavern: its area (cells) and its depth-weighted volume
// (sum of t - S). Alignments tried: the 24 lowest spots of each stack's 1280-block means, all pairs, offsets -2 .. 2.
// Prints per seed: for t = -0.24, -0.28, -0.32, -0.36: the largest area, the largest volume (over all alignments), and
// the volume of the 4th best pair of spots.
// build: g++ -O3 -march=native -std=c++17 -I . -I vendor/cubiomes stats/percfeatures.cpp vendor/cub_noise.o -lpthread -o stats/percfeatures
#include "analyticgate.hpp"
#include <atomic>
#include <cstdio>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
static const int G = 256, SPOTS = 24, SIDE = 12, NT = 4; static const float T[NT] = {-0.24f, -0.28f, -0.32f, -0.36f};
struct Spot { float v; int i, j; };
static void spots(const std::vector<float> &w, std::vector<Spot> &out) {
    std::vector<Spot> all; all.reserve(G * G);
    for (int i = 0; i < G; i++) for (int j = 0; j < G; j++) all.push_back({w[(size_t) i * G + j], i, j});
    std::partial_sort(all.begin(), all.begin() + 4000, all.end(), [](const Spot &a, const Spot &b) { return a.v < b.v; });
    out.clear();
    for (int k = 0; k < 4000 && (int) out.size() < SPOTS; k++) {
        bool far = true;
        for (const Spot &s : out) { int di = std::abs(all[k].i - s.i), dj = std::abs(all[k].j - s.j); di = std::min(di, G - di); dj = std::min(dj, G - dj); if (di <= 3 && dj <= 3) { far = false; break; } }
        if (far) out.push_back(all[k]);
    }
}
int main(int argc, char **argv) {
    int threads = argc > 1 ? atoi(argv[1]) : 8;
    std::vector<uint64_t> seeds; std::string l; while (std::getline(std::cin, l)) if (!l.empty()) seeds.push_back((uint64_t) std::stoll(l));
    std::atomic<size_t> next(0); std::mutex lock; std::vector<std::thread> pool;
    for (int t = 0; t < threads; t++) pool.emplace_back([&] {
        AnalyticGate gate; const int N = AnalyticGate::N; std::vector<float> ga(G * G), gb(G * G), wa(G * G), wb(G * G); std::vector<Spot> sa, sb;
        for (size_t k; (k = next++) < seeds.size();) {
            NormalNoise cheese = makeNoise(seeds[k], "minecraft:cave_cheese"); float lowest, value = gate.value(cheese, lowest), wide = gate.wideValue();
            for (int i = 0; i < G; i++) for (int j = 0; j < G; j++) {
                ga[i * G + j] = gate.field[0][(size_t) (2 * i) * N + 2 * j]; gb[i * G + j] = gate.field[1][(size_t) (2 * i) * N + 2 * j];
                wa[i * G + j] = gate.wide[0][(size_t) (2 * i) * N + 2 * j]; wb[i * G + j] = gate.wide[1][(size_t) (2 * i) * N + 2 * j];
            }
            spots(wa, sa); spots(wb, sb);
            float bestArea[NT] = {0}, bestVol[NT] = {0}; std::vector<float> pairVol[NT];
            for (const Spot &a : sa) for (const Spot &b : sb) {
                float volHere[NT] = {0};
                for (int dx = -2; dx <= 2; dx++) for (int dz = -2; dz <= 2; dz++) {
                    float s[SIDE][SIDE];
                    for (int u = 0; u < SIDE; u++) for (int v = 0; v < SIDE; v++) s[u][v] = ga[((a.i + u - 4) & 255) * G + ((a.j + v - 4) & 255)] + gb[((b.i + dx + u - 4) & 255) * G + ((b.j + dz + v - 4) & 255)];
                    for (int q = 0; q < NT; q++) {
                        uint16_t open[SIDE], in[SIDE] = {0};
                        for (int u = 0; u < SIDE; u++) { uint16_t m = 0; for (int v = 0; v < SIDE; v++) if (s[u][v] < T[q]) m |= (uint16_t) (1u << v); open[u] = m; }
                        in[5] = open[5] & 0x60; in[6] = open[6] & 0x60; // the four centre cells
                        if (!(in[5] | in[6])) continue;
                        for (bool grew = true; grew;) {
                            grew = false;
                            for (int u = 0; u < SIDE; u++) {
                                uint16_t m = in[u] | (uint16_t) (in[u] << 1) | (in[u] >> 1); if (u > 0) m |= in[u - 1]; if (u + 1 < SIDE) m |= in[u + 1];
                                m &= open[u]; if (m != in[u]) { in[u] = m; grew = true; }
                            }
                        }
                        int area = 0; float vol = 0;
                        for (int u = 0; u < SIDE; u++) for (int v = 0; v < SIDE; v++) if (in[u] >> v & 1) { area++; vol += T[q] - s[u][v]; }
                        bestArea[q] = std::max(bestArea[q], (float) area); bestVol[q] = std::max(bestVol[q], vol); volHere[q] = std::max(volHere[q], vol);
                    }
                }
                for (int q = 0; q < NT; q++) pairVol[q].push_back(volHere[q]);
            }
            float fourth[NT];
            for (int q = 0; q < NT; q++) { std::nth_element(pairVol[q].begin(), pairVol[q].begin() + 3, pairVol[q].end(), std::greater<float>()); fourth[q] = pairVol[q][3]; }
            std::lock_guard<std::mutex> guard(lock);
            printf("%lld %.4f %.4f", (long long) seeds[k], value, wide);
            for (int q = 0; q < NT; q++) printf(" %.0f %.3f %.3f", bestArea[q], bestVol[q], fourth[q]);
            printf("\n");
        }
    });
    for (auto &t : pool) t.join();
}
