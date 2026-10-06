// The percolation term of the seed gate. The giant caverns are percolation clusters: what counts is not how low the
// smooth noise is on average over a square window but how big the *connected* region is where it stays under a
// critical level, whatever its shape. For an alignment of the two stacks (every offset between them exists somewhere in
// the world: the pair trick) the smooth field is S = A + B, A and B being the 512-block window means of the two lowest
// cave_cheese octaves (analyticgate.hpp) on a grid of 256 blocks. Over a neighbourhood of 12 x 12 cells (3,072 blocks)
// the cells with S < -0.16 that hang together with the centre (4 neighbours) are the candidate cavern, and its volume is
// the sum of -0.16 - S over them. Alignments: the 24 lowest spots (4 cells apart at least) of each stack's 1280-block
// means, every pair of spots, offsets of -2 .. 2 cells; a pair's volume is its best offset's. The number returned is
// the mean volume of the 8 best pairs: a seed needs several good alignments, because each one meets different terrain.
// Over 32,663 seeds that had passed the gate score (run g9, 121 caverns of 36M+), adding 0.091 x this to the score put
// 42% of those caverns in the best tenth of the seeds (score alone: 31%) and left 8% in the worse half (26%).
#pragma once
#include "analyticgate.hpp"
#include <algorithm>
#include <vector>
struct Percolation {
    static constexpr int G = 256, SPOTS = 24, SIDE = 12, SHIFTS = 25, CELLS = SIDE * SIDE, BEST = 8;
    static constexpr float LEVEL = -0.16f;
    struct Spot { float v; int i, j; };
    std::vector<float> ga, gb, wa, wb, patchA, patchB, volumes; std::vector<Spot> all, sa, sb;
    Percolation() : ga(G * G), gb(G * G), wa(G * G), wb(G * G), patchA((size_t) SPOTS * CELLS), patchB((size_t) SPOTS * SHIFTS * CELLS) {}
    void spots(const std::vector<float> &w, std::vector<Spot> &out) {
        all.clear();
        for (int i = 0; i < G; i++) for (int j = 0; j < G; j++) all.push_back({w[(size_t) i * G + j], i, j});
        std::partial_sort(all.begin(), all.begin() + 4000, all.end(), [](const Spot &a, const Spot &b) { return a.v < b.v; });
        out.clear();
        for (int k = 0; k < 4000 && (int) out.size() < SPOTS; k++) {
            bool far = true;
            for (const Spot &s : out) { int di = std::abs(all[k].i - s.i), dj = std::abs(all[k].j - s.j); di = std::min(di, G - di); dj = std::min(dj, G - dj); if (di <= 3 && dj <= 3) { far = false; break; } }
            if (far) out.push_back(all[k]);
        }
    }
    // after gate.value() and gate.wideValue(): the mean connected volume of the 8 best pairs of spots
    float volume(const AnalyticGate &gate) {
        const int N = AnalyticGate::N;
        for (int i = 0; i < G; i++) for (int j = 0; j < G; j++) {
            ga[i * G + j] = gate.field[0][(size_t) (2 * i) * N + 2 * j]; gb[i * G + j] = gate.field[1][(size_t) (2 * i) * N + 2 * j];
            wa[i * G + j] = gate.wide[0][(size_t) (2 * i) * N + 2 * j]; wb[i * G + j] = gate.wide[1][(size_t) (2 * i) * N + 2 * j];
        }
        spots(wa, sa); spots(wb, sb);
        for (size_t a = 0; a < sa.size(); a++) for (int u = 0; u < SIDE; u++) for (int v = 0; v < SIDE; v++) patchA[a * CELLS + u * SIDE + v] = ga[((sa[a].i + u - 4) & 255) * G + ((sa[a].j + v - 4) & 255)];
        for (size_t b = 0; b < sb.size(); b++) for (int dx = -2; dx <= 2; dx++) for (int dz = -2; dz <= 2; dz++) {
            float *p = &patchB[(b * SHIFTS + (dx + 2) * 5 + dz + 2) * CELLS];
            for (int u = 0; u < SIDE; u++) for (int v = 0; v < SIDE; v++) p[u * SIDE + v] = gb[((sb[b].i + dx + u - 4) & 255) * G + ((sb[b].j + dz + v - 4) & 255)];
        }
        volumes.clear();
        for (size_t a = 0; a < sa.size(); a++) for (size_t b = 0; b < sb.size(); b++) {
            const float *pa = &patchA[a * CELLS]; float best = 0;
            for (int shift = 0; shift < SHIFTS; shift++) {
                const float *pb = &patchB[(b * SHIFTS + shift) * CELLS]; float s[CELLS]; uint16_t open[SIDE], in[SIDE] = {0};
                for (int c = 0; c < CELLS; c++) s[c] = pa[c] + pb[c];
                for (int u = 0; u < SIDE; u++) { uint16_t m = 0; for (int v = 0; v < SIDE; v++) if (s[u * SIDE + v] < LEVEL) m |= (uint16_t) (1u << v); open[u] = m; }
                in[5] = open[5] & 0x60; in[6] = open[6] & 0x60; // the four centre cells
                if (!(in[5] | in[6])) continue;
                for (bool grew = true; grew;) {
                    grew = false;
                    for (int u = 0; u < SIDE; u++) {
                        uint16_t m = in[u] | (uint16_t) (in[u] << 1) | (in[u] >> 1); if (u > 0) m |= in[u - 1]; if (u + 1 < SIDE) m |= in[u + 1];
                        m &= open[u]; if (m != in[u]) { in[u] = m; grew = true; }
                    }
                }
                float vol = 0;
                for (int u = 0; u < SIDE; u++) for (int v = 0; v < SIDE; v++) if (in[u] >> v & 1) vol += LEVEL - s[u * SIDE + v];
                best = std::max(best, vol);
            }
            volumes.push_back(best);
        }
        if ((int) volumes.size() < BEST) return 0;
        std::partial_sort(volumes.begin(), volumes.begin() + BEST, volumes.end(), std::greater<float>());
        float sum = 0; for (int k = 0; k < BEST; k++) sum += volumes[k];
        return sum / BEST;
    }
};
