// icecaves/icelobe.cpp: the ice caves around a place, as a footprint: how large is the patch of columns that are ice
// caves over the whole height of a cavern, and what ends it?
// stdin: "SEED X Z [anything]" per line. For each, columns on a 32-block grid within R blocks (default 960) get the
// climate (the game's values without the shift noise and the finest octaves) and the ice caves cells of the column.
// A column is a "slab" column when every cell from y -56 to y 16 is ice caves (the heights a cavern takes), a "floor"
// column when the cells at y -56 and -52 are, a "middle" column when the cell at y -16 is.
// stdout per line: "SEED X Z | slab A floor A middle A | reach R | ends w c t d | c C floor F w W t T | rest"
//   A: blocks^2 of the patch of such columns connected to the place (through the nearest such column within 96 blocks);
//   reach: the largest square, in blocks a side, of slab columns centred within 192 blocks of the place;
//   ends: of the columns just outside the slab patch, the share that fail by weirdness, continentalness, temperature,
//   depth (the number farthest outside the ice caves box at the floor); then means over the slab patch: continentalness,
//   sea floor y, weirdness, temperature.
// build: g++ -O3 -march=native -std=c++17 -ffp-contract=off -I ../vendor/cubiomes icelobe.cpp icebiome.o ../vendor/cub_*.o -lpthread -o icelobe
#include "icebiome.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

int main(int argc, char **argv) {
    const int R = argc > 1 ? atoi(argv[1]) : 960, STEP = 32, N = 2 * (R / STEP) + 1, H = N / 2;
    char line[1024]; unsigned long long lastSeed = 0; IceBiome *b = NULL;
    std::vector<IceClimate> cl((size_t) N * N); std::vector<uint8_t> slab((size_t) N * N), flo((size_t) N * N), mid((size_t) N * N), seen((size_t) N * N); std::vector<int> stack;
    while (fgets(line, sizeof line, stdin)) {
        long long seed; long x, z; int used = 0;
        if (sscanf(line, "%lld %ld %ld%n", &seed, &x, &z, &used) < 3) continue;
        if (!b || (unsigned long long) seed != lastSeed) { if (b) iceFree(b); b = iceNew((unsigned long long) seed, false); lastSeed = (unsigned long long) seed; }
        for (int i = 0; i < N; i++) for (int j = 0; j < N; j++) {
            IceClimate &c = cl[(size_t) i * N + j]; iceClimate(b, (int) ((x + (i - H) * STEP) >> 2), (int) ((z + (j - H) * STEP) >> 2), &c, false, true);
            uint64_t bits[2]; iceColumn(&c, bits, 0);
            auto at = [&](int qy) { return (bits[(qy + 16) >> 6] >> ((qy + 16) & 63)) & 1; };
            bool all = true; for (int qy = -14; qy <= 4; qy++) all &= at(qy) != 0;
            slab[(size_t) i * N + j] = all; flo[(size_t) i * N + j] = at(-14) && at(-13); mid[(size_t) i * N + j] = at(-4);
        }
        auto patch = [&](const std::vector<uint8_t> &m, std::vector<int> *members) {   // the patch connected to the nearest set column within 3 columns of the middle
            int start = -1, best = 99;
            for (int di = -3; di <= 3; di++) for (int dj = -3; dj <= 3; dj++) { const int i = H + di, j = H + dj, d = abs(di) + abs(dj); if (m[(size_t) i * N + j] && d < best) { best = d; start = i * N + j; } }
            if (start < 0) return 0;
            std::fill(seen.begin(), seen.end(), 0); stack.clear(); stack.push_back(start); seen[start] = 1; int count = 0;
            while (!stack.empty()) {
                const int p = stack.back(); stack.pop_back(); count++; if (members) members->push_back(p); const int i = p / N, j = p % N;
                const int nb[4][2] = {{i - 1, j}, {i + 1, j}, {i, j - 1}, {i, j + 1}};
                for (auto &q : nb) if (q[0] >= 0 && q[0] < N && q[1] >= 0 && q[1] < N) { const int r = q[0] * N + q[1]; if (m[r] && !seen[r]) { seen[r] = 1; stack.push_back(r); } }
            }
            return count;
        };
        std::vector<int> members; const int aSlab = patch(slab, &members), aFloor = patch(flo, NULL), aMid = patch(mid, NULL);
        // the largest square of slab columns centred within 6 columns of the place
        int reach = 0;
        for (int ci = H - 6; ci <= H + 6; ci++) for (int cj = H - 6; cj <= H + 6; cj++) {
            int r = 0;
            for (;; r++) { bool ok = ci - r >= 0 && ci + r < N && cj - r >= 0 && cj + r < N; for (int k = -r; k <= r && ok; k++) ok = slab[(size_t) (ci - r) * N + cj + k] && slab[(size_t) (ci + r) * N + cj + k] && slab[(size_t) (ci + k) * N + cj - r] && slab[(size_t) (ci + k) * N + cj + r]; if (!ok) break; }
            reach = std::max(reach, 2 * r - 1);
        }
        // what ends the slab patch, and its means
        double ends[4] = {0, 0, 0, 0}, sc = 0, sf = 0, sw = 0, st = 0; int edge = 0; std::fill(seen.begin(), seen.end(), 0);
        for (int p : members) seen[p] = 1;
        for (int p : members) {
            const IceClimate &c = cl[p]; sc += c.c; sf += 128.0 * (1.0 - 83.0 / 160.0 + c.off); sw += c.w; st += c.t; const int i = p / N, j = p % N;
            const int nb[4][2] = {{i - 1, j}, {i + 1, j}, {i, j - 1}, {i, j + 1}};
            for (auto &q : nb) if (q[0] >= 0 && q[0] < N && q[1] >= 0 && q[1] < N) {
                const int r = q[0] * N + q[1]; if (seen[r]) continue;
                const IceClimate &o = cl[r]; const double depth = 1.0 - 83.0 / 160.0 + o.off + 54.0 / 128.0;
                const double far[4] = {std::max(0.0, 0.5 - (double) o.w), std::max({0.0, (double) o.c + 0.19, -1.05 - (double) o.c}), std::max(0.0, (double) o.t + 0.8), std::max({0.0, 0.2 - depth, depth - 0.9})};
                int worst = 0; for (int k = 1; k < 4; k++) if (far[k] > far[worst]) worst = k;
                ends[worst]++; edge++;
            }
        }
        const int n = std::max(1, (int) members.size()), e = std::max(1, edge); line[strcspn(line, "\n")] = 0;
        printf("%lld %ld %ld | slab %d floor %d middle %d | reach %d | ends %.2f %.2f %.2f %.2f | c %.3f floor %.1f w %.3f t %.3f |%s\n", seed, x, z, aSlab * STEP * STEP, aFloor * STEP * STEP, aMid * STEP * STEP, std::max(0, reach) * STEP,
               ends[0] / e, ends[1] / e, ends[2] / e, ends[3] / e, sc / n, sf / n, sw / n, st / n, line + used);
    }
    return 0;
}
