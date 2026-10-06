// The table gate without tables of noise samples: the mean of a Perlin layer over a box is a closed form in the
// gradients at the lattice corners, so the window means of the two lowest cave_cheese octaves come straight from the
// permutation tables.
//
// In a lattice cell, with local coordinates (u, v, w) in [0, 1] and the fade t(s) = 6 s^5 - 15 s^4 + 10 s^3, Perlin noise is
//     sum over the 8 corners (i, j, k) of  T_i(u) T_j(v) T_k(w) g . (u - i, v - j, w - k),    T_0 = 1 - t, T_1 = t,
// g being the corner's gradient. Its integral over a box inside the cell factors into 1D integrals of T_i(s) and
// T_i(s) (s - i), which are polynomials. A window is a few (part) cells per axis, so its integral is a sum over lattice
// corners of the gradient components times per-axis weights:
//     sum over corners of  gx Bx Ay Az + gy Ax By Az + gz Ax Ay Bz.
// The y range is the same for every window, so it folds into three 256 x 256 arrays per layer; the x and z weights
// are the same for every window too when the windows step by half a cell of octave -8 (= a cell of octave -7, 128
// blocks), so the rest is two short convolutions. Windows: 512 x 512 blocks (502 for the B stack, whose cells are
// 1.018 times smaller), 512 x 512 of them per stack, heights Y_LO .. Y_HI.

struct AnalyticGate {
    static constexpr int N = 512;                      // windows per axis and stack
#ifndef GATE_Y_LO
#define GATE_Y_LO -56
#define GATE_Y_HI 72
#endif
    static constexpr double Y_LO = GATE_Y_LO, Y_HI = GATE_Y_HI;     // the heights averaged over: where big caverns are (other ranges: for stats/gatefeatures only)
    static constexpr size_t PAIRS = 184700;            // the share of all pairs that cavepairs keeps (300,000 of 1011^2 x 993^2 / 9), for 512^2 x 512^2
    std::vector<float> field[2], gx, gy, gz, hx, hy, hz; std::vector<uint32_t> histogram[2];
    AnalyticGate() : gx(256 * 256), gy(256 * 256), gz(256 * 256) { field[0].resize((size_t) N * N); field[1].resize((size_t) N * N); }

    // integrals over [s0, s1] of T_i(s) (into a[i]) and T_i(s) (s - i) (into b[i]), added
    static void integrals(double s0, double s1, double *a, double *b) {
        auto pa = [](double s) { double s4 = s * s * s * s; return s4 * (s * s - 3.0 * s + 2.5); };            // integral of t
        auto pb = [](double s) { double s5 = s * s * s * s * s; return s5 * (6.0 / 7.0 * s * s - 2.5 * s + 2.0); }; // integral of t s
        double ta = pa(s1) - pa(s0), tb = pb(s1) - pb(s0), length = s1 - s0, moment = 0.5 * (s1 * s1 - s0 * s0);
        a[1] += ta; a[0] += length - ta; b[1] += tb - ta; b[0] += moment - tb;
    }
    // the weights of the lattice lines floor(lo) .. floor(lo) + count - 1 for the range lo .. hi (count >= cells touched + 1)
    static int weights(double lo, double hi, double *wa, double *wb, int count) {
        int first = (int) std::floor(lo);
        for (int i = 0; i < count; i++) wa[i] = wb[i] = 0;
        for (int c = first; c < hi && c - first + 1 < count; c++) {
            double s0 = std::max(lo, (double) c) - c, s1 = std::min(hi, c + 1.0) - c, a[2] = {0, 0}, b[2] = {0, 0};
            if (s1 <= s0) continue;
            integrals(s0, s1, a, b);
            wa[c - first] += a[0]; wb[c - first] += b[0]; wa[c - first + 1] += a[1]; wb[c - first + 1] += b[1];
        }
        return first;
    }
    // adds amplitude x (window means of one layer) to out[N * N]; cellsPerStep: 0.5 for octave -8, 1 for octave -7
    void addLayer(const NoiseLayer &layer, double yScale, double cellsPerStep, std::vector<float> &out) {
        static const float GX[16] = {1, -1, 1, -1, 1, -1, 1, -1, 0, 0, 0, 0, 1, 0, -1, 0}, GY[16] = {1, 1, -1, -1, 0, 0, 0, 0, 1, -1, 1, -1, 1, -1, 1, -1}, GZ[16] = {0, 0, 0, 0, 1, 1, -1, -1, 1, 1, -1, -1, 0, 1, 0, -1};
        const PerlinNoise &p = layer.perlin; const uint8_t *d = p.d;
        // y: the same range for every window
        double yLo = Y_LO * yScale * layer.frequency + p.b, yHi = Y_HI * yScale * layer.frequency + p.b, wyA[4], wyB[4];
        int y0 = weights(yLo, yHi, wyA, wyB, 4), planes = (int) std::floor(yHi) - y0 + 2;
        for (int x = 0; x < 256; x++) for (int z = 0; z < 256; z++) {
            float sx = 0, sy = 0, sz = 0;
            for (int k = 0; k < planes; k++) {
                uint8_t h = d[(uint8_t) (d[(uint8_t) (d[x] + (uint8_t) (y0 + k))] + z)] & 15;
                sx += GX[h] * (float) wyA[k]; sy += GY[h] * (float) wyB[k]; sz += GZ[h] * (float) wyA[k];
            }
            gx[x * 256 + z] = sx; gy[x * 256 + z] = sy; gz[x * 256 + z] = sz;
        }
        // x and z: the window of index m starts at m x cellsPerStep (+ the layer's offset) and is 4 steps long
        const int variants = cellsPerStep < 1 ? 2 : 1, perCell = variants, period = 256 * perCell, taps = (int) (4 * cellsPerStep) + 2;
        double length = 4 * cellsPerStep, norm = layer.amplitude / (length * length * (yHi - yLo));
        double kxA[2][8], kxB[2][8], kzA[2][8], kzB[2][8]; int baseX[2], baseZ[2];
        for (int e = 0; e < variants; e++) {
            baseX[e] = weights(p.a + e * cellsPerStep, p.a + e * cellsPerStep + length, kxA[e], kxB[e], taps);
            baseZ[e] = weights(p.c + e * cellsPerStep, p.c + e * cellsPerStep + length, kzA[e], kzB[e], taps);
        }
        hx.assign((size_t) period * 256, 0); hy.assign((size_t) period * 256, 0); hz.assign((size_t) period * 256, 0);
        for (int m = 0; m < period; m++) { // along x: for every window start and corner line z
            int e = m % variants, cell = m / perCell; float *ox = &hx[(size_t) m * 256], *oy = &hy[(size_t) m * 256], *oz = &hz[(size_t) m * 256];
            for (int r = 0; r < taps; r++) {
                int x = (baseX[e] + cell + r) & 255; float a = (float) kxA[e][r], b = (float) kxB[e][r];
                const float *ix = &gx[x * 256], *iy = &gy[x * 256], *iz = &gz[x * 256];
                for (int z = 0; z < 256; z++) { ox[z] += b * ix[z]; oy[z] += a * iy[z]; oz[z] += a * iz[z]; }
            }
        }
        // along z: for one kernel variant and one tap, the windows of a row read consecutive corner lines, so each tap
        // is one run over a copy of the line that is laid out twice (no wrap inside the run)
        float xy[512], zz[512], acc[256], row[512];
        for (int m = 0; m < period; m++) {
            const float *ix = &hx[(size_t) m * 256], *iy = &hy[(size_t) m * 256], *iz = &hz[(size_t) m * 256];
            for (int z = 0; z < 256; z++) { xy[z] = xy[z + 256] = ix[z] + iy[z]; zz[z] = zz[z + 256] = iz[z]; }
            for (int e = 0; e < variants; e++) {
                for (int c = 0; c < 256; c++) acc[c] = 0;
                for (int r = 0; r < taps; r++) {
                    const float ka = (float) kzA[e][r], kb = (float) kzB[e][r], *sa = xy + ((baseZ[e] + r) & 255), *sb = zz + ((baseZ[e] + r) & 255);
                    for (int c = 0; c < 256; c++) acc[c] += ka * sa[c] + kb * sb[c];
                }
                for (int c = 0; c < 256; c++) row[c * perCell + e] = acc[c] * (float) norm;
            }
            for (int mm = m; mm < N; mm += period) { float *o = &out[(size_t) mm * N]; for (int n = 0; n < N; n += period) for (int i = 0; i < period; i++) o[n + i] += row[i]; }
        }
    }
    // the smallest window-mean sum a + b that at least `pairs` pairs stay under, and the lowest sum. The pair count comes
    // from histograms of the two fields (bins of 1/4096): pairs with a sum of at most T bins = sum over A bins of the
    // B values in the bins up to T - a.
    float quantile(const std::vector<float> &a, const std::vector<float> &b, uint64_t pairs, float &lowest) {
        static constexpr int BINS = 4096; static constexpr float PER_UNIT = 4096.0f; // one unit of noise above each field's minimum
        float least[2];
        for (int half = 0; half < 2; half++) {
            const std::vector<float> &f = half ? b : a; float low = f[0];
            for (float v : f) low = std::min(low, v);
            least[half] = low; histogram[half].assign(BINS, 0);
            for (float v : f) { int bin = (int) ((v - low) * PER_UNIT); if (bin < BINS) histogram[half][bin]++; }
        }
        for (int i = 1; i < BINS; i++) histogram[1][i] += histogram[1][i - 1]; // B: cumulative
        auto pairsBelow = [&](int total) { // pairs whose bins add up to at most total
            uint64_t count = 0;
            for (int i = 0; i <= total && i < BINS; i++) if (histogram[0][i]) count += (uint64_t) histogram[0][i] * histogram[1][std::min(total - i, BINS - 1)];
            return count;
        };
        int lo = 0, hi = BINS - 1; // the smallest bin sum with enough pairs
        while (lo < hi) { int mid = (lo + hi) / 2; if (pairsBelow(mid) >= pairs) hi = mid; else lo = mid + 1; }
        lowest = least[0] + least[1];
        return lowest + (lo + 1) / PER_UNIT;
    }
    // the gate value: the window-mean sum A + B at the pair count that matches cavepairs' cut
    float value(const NormalNoise &cheese, float &lowest) {
        for (int half = 0; half < 2; half++) {
            std::fill(field[half].begin(), field[half].end(), 0.0f);
            addLayer(cheese.layers[half], 0.6666666666666666, 0.5, field[half]);      // octave -8
            addLayer(cheese.layers[2 + half], 0.6666666666666666, 1.0, field[half]);  // octave -7
        }
        return quantile(field[0], field[1], PAIRS, lowest);
    }
    // The wide gate (call after value(): it works on its fields). The biggest caverns are 1,500 to 2,400 blocks long, and
    // how many of them a seed has follows the cheese noise averaged over a window of that order far better than over
    // 512 blocks: among 15,000 seeds that had passed the 512-block gate, the best tenth by this number held two thirds of
    // the big sites. Windows: 1280 blocks, tapered (the mean of 4 x 4 of the 512-block windows, 256 blocks apart);
    // the value is the window-mean sum at pair number 1,000 (of 512^2 x 512^2).
    static constexpr size_t WIDE_PAIRS = 1000;
    std::vector<float> wide[2], strip;
    float wideValue() {
        strip.resize((size_t) N * N);
        for (int half = 0; half < 2; half++) {
            wide[half].resize((size_t) N * N); const float *f = field[half].data();
            for (int i = 0; i < N; i++) { // a row with its start repeated at the end: no wrap inside the sum
                float line[N + 8]; const float *row = f + (size_t) i * N, *o2 = line + 2, *o4 = line + 4, *o6 = line + 6; float *out = &strip[(size_t) i * N];
                for (int j = 0; j < N; j++) line[j] = row[j];
                for (int j = 0; j < 8; j++) line[N + j] = row[j];
                for (int j = 0; j < N; j++) out[j] = line[j] + o2[j] + o4[j] + o6[j];
            }
            for (int i = 0; i < N; i++) {
                const float *r0 = &strip[(size_t) i * N], *r1 = &strip[(size_t) ((i + 2) & (N - 1)) * N], *r2 = &strip[(size_t) ((i + 4) & (N - 1)) * N], *r3 = &strip[(size_t) ((i + 6) & (N - 1)) * N]; float *o = &wide[half][(size_t) i * N];
                for (int j = 0; j < N; j++) o[j] = (r0[j] + r1[j] + r2[j] + r3[j]) * 0.0625f;
            }
        }
        float lowest; return quantile(wide[0], wide[1], WIDE_PAIRS, lowest);
    }
};

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
