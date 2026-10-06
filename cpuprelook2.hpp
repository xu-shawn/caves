// The coarse looks of cpuprelook.hpp (same numbers, same interface) with the z filter taken out of the loops.
// A lattice line of a layer, at x index i and y plane k, reads the table's gradients d[(e + z) & 255] along z, where
// e = d[(d[x] + y) & 255] is one byte per line and plane: every line is the same 256 gradients, turned by e. So the
// windows' weighted sum along z (7 or 12 taps) is made ONCE on that ring of 256 gradients, per component:
//     AX[j] = sum_r kzA[r] GX[d[j + r]],   AY[j] = sum_r kzA[r] GY[d[j + r]],   AZ[j] = sum_r kzB[r] GZ[d[j + r]]
// and a line, already summed over a window along z, is for window column t (stride `step` cells)
//     P_i[t] = sum_k wyA[k] AX[(e_k + step t) & 255]            (x component)
//     Q_i[t] = sum_k (wyB[k] AY + wyA[k] AZ)[(e_k + step t) & 255]   (y and z components)
// that is, per plane, one contiguous read of a table stored by phase of the stride and added. A window is then the
// weighted sum of its 7 (12) lines along x, on 128 (64) window columns instead of 256 lattice columns, and the pass
// along z per row is gone. The old code made each line from 256 gradient lookups per plane and filtered every row
// along z; this makes a line from `planes` vector adds per 8 (16) columns. Values equal the old ones up to rounding
// (single precision: the order of the sums; 16 bit: the table entries are rounded where the lines were).
#pragma once
#include "tableseed.hpp"
#include <immintrin.h>
struct CoarseLook2 {
    static const int RING = 16;
    alignas(64) float tableP[4][4][272], tableQ[4][4][272]; // [plane][phase of the stride][window column, twice round]
    alignas(64) float ringP[RING][128], ringQ[RING][128];
    alignas(64) float field8[2][128 * 128], field7[64 * 64];
    alignas(64) float gx[272], gy[272], gz[272], ax[256], aq[4][256], ap[4][256], halfF[2][144];
    static inline void split(const float *in, int pairs, float *even, float *odd) { // in[2 i] and in[2 i + 1] apart, pairs a multiple of 8
        for (int i = 0; i < pairs; i += 8) {
            __m256 a = _mm256_loadu_ps(in + 2 * i), b = _mm256_loadu_ps(in + 2 * i + 8);
            _mm256_storeu_ps(even + i, _mm256_castpd_ps(_mm256_permute4x64_pd(_mm256_castps_pd(_mm256_shuffle_ps(a, b, 0x88)), 0xD8)));
            _mm256_storeu_ps(odd + i, _mm256_castpd_ps(_mm256_permute4x64_pd(_mm256_castps_pd(_mm256_shuffle_ps(a, b, 0xDD)), 0xD8)));
        }
    }
    // table[phase][i] = full[(phase + step i) & 255] for i < 2 x period (so a read of `period` entries from any start below `period` does not wrap)
    static inline void byPhase(const float *full, int step, float (*table)[272], float (*scratch)[144]) {
        if (step == 2) split(full, 128, table[0], table[1]);
        else { split(full, 128, scratch[0], scratch[1]); split(scratch[0], 64, table[0], table[2]); split(scratch[1], 64, table[1], table[3]); }
        const int period = 256 / step;
        for (int phase = 0; phase < step; phase++) memcpy(table[phase] + period, table[phase], period * sizeof(float));
    }
    // the window means of one layer on the coarse grid into out[m * period + t] (period 128 for octave -8, 64 for -7); returns the lowest
    float layer(const TableLayer &L, int octave, float *out) {
        static const float GX[16] = {1, -1, 1, -1, 1, -1, 1, -1, 0, 0, 0, 0, 1, 0, -1, 0}, GY[16] = {1, 1, -1, -1, 0, 0, 0, 0, 1, -1, 1, -1, 1, -1, 1, -1}, GZ[16] = {0, 0, 0, 0, 1, 1, -1, -1, 1, 1, -1, -1, 0, 1, 0, -1};
        const int step = octave ? 4 : 2, sub = octave ? 2 : 1, base = octave ? 6 : 4, taps = base + 3 * sub, period = 256 / step, planes = L.planes;
        float kxa[12] = {0}, kxb[12] = {0}, kza[12] = {0}, kzb[12] = {0};
        for (int j = 0; j < 4; j++) for (int r = 0; r < base; r++) { kxa[r + sub * j] += L.kxA[r]; kxb[r + sub * j] += L.kxB[r]; kza[r + sub * j] += L.kzA[r]; kzb[r + sub * j] += L.kzB[r]; }
        for (int j = 0; j < 272; j++) { const int h = L.d[j & 255] & 15; gx[j] = GX[h]; gy[j] = GY[h]; gz[j] = GZ[h]; }
        for (int j = 0; j < 256; j += 8) { // the z filter, once
            __m256 sx = _mm256_setzero_ps(), sy = sx, sz = sx;
            for (int r = 0; r < taps; r++) { const __m256 a = _mm256_set1_ps(kza[r]); sx = _mm256_fmadd_ps(a, _mm256_loadu_ps(gx + j + r), sx); sy = _mm256_fmadd_ps(a, _mm256_loadu_ps(gy + j + r), sy); sz = _mm256_fmadd_ps(_mm256_set1_ps(kzb[r]), _mm256_loadu_ps(gz + j + r), sz); }
            _mm256_store_ps(ax + j, sx);
            for (int k = 0; k < planes; k++) { _mm256_store_ps(ap[k] + j, _mm256_mul_ps(_mm256_set1_ps(L.wyA[k]), sx)); _mm256_store_ps(aq[k] + j, _mm256_fmadd_ps(_mm256_set1_ps(L.wyB[k]), sy, _mm256_mul_ps(_mm256_set1_ps(L.wyA[k]), sz))); }
        }
        for (int k = 0; k < planes; k++) { byPhase(ap[k], step, tableP[k], halfF); byPhase(aq[k], step, tableQ[k], halfF); }
        const __m256 norm = _mm256_set1_ps(L.norm * 0.0625f); __m256 lowest = _mm256_set1_ps(1e9f); int have = 0;
        for (int m = 0; m < period; m++) {
            for (; have < step * m + taps; have++) { // line `have`: per plane a turned copy of the tables, added
                const uint8_t dx = L.d[(L.baseX + have) & 255]; float *P = ringP[have & (RING - 1)], *Q = ringQ[have & (RING - 1)]; const float *sp[4], *sq[4];
                for (int k = 0; k < planes; k++) { const int e = (L.d[(uint8_t) (dx + (uint8_t) (L.y0 + k))] + L.baseZ) & 255; sp[k] = tableP[k][e % step] + e / step; sq[k] = tableQ[k][e % step] + e / step; }
                for (int t = 0; t < period; t += 8) {
                    __m256 p = _mm256_loadu_ps(sp[0] + t), q = _mm256_loadu_ps(sq[0] + t);
                    for (int k = 1; k < planes; k++) { p = _mm256_add_ps(p, _mm256_loadu_ps(sp[k] + t)); q = _mm256_add_ps(q, _mm256_loadu_ps(sq[k] + t)); }
                    _mm256_store_ps(P + t, p); _mm256_store_ps(Q + t, q);
                }
            }
            float *o = out + m * period;
            for (int t = 0; t < period; t += 8) {
                __m256 sum = _mm256_setzero_ps();
                for (int r = 0; r < taps; r++) { const int slot = (step * m + r) & (RING - 1); sum = _mm256_fmadd_ps(_mm256_set1_ps(kxb[r]), _mm256_load_ps(ringP[slot] + t), sum); sum = _mm256_fmadd_ps(_mm256_set1_ps(kxa[r]), _mm256_load_ps(ringQ[slot] + t), sum); }
                sum = _mm256_mul_ps(sum, norm); lowest = _mm256_min_ps(lowest, sum); _mm256_storeu_ps(o + t, sum);
            }
        }
        alignas(32) float lanes[8]; _mm256_store_ps(lanes, lowest); float low = lanes[0];
        for (int i = 1; i < 8; i++) low = std::min(low, lanes[i]);
        return low;
    }
    // The y components alone in 16-bit fixed point (the first two looks). Scales as in cpuprelook.hpp: a window is in
    // units of (16 or 64) / scale x norm / 16, where scale = 8000 / (sum of the |y weights|); here the table entries
    // carry scale / 4 (octave -8) or scale / 8 (-7) with the z kernel already in them, a line is a sum of `planes`
    // entries, and a window is lines x kernel / 4 (8), the sums saturating.
    alignas(64) int16_t table16[4][4][272], ring16[RING][128], windows8[2][128 * 128], windows7[64 * 64], full16[256], half16[2][144]; float unit8[2];
    static inline void split16(const int16_t *in, int pairs, int16_t *even, int16_t *odd) {
        const __m256i WORD = _mm256_set1_epi32(0xFFFF);
        for (int i = 0; i < pairs; i += 16) {
            const __m256i a = _mm256_loadu_si256((const __m256i *) (in + 2 * i)), b = _mm256_loadu_si256((const __m256i *) (in + 2 * i + 16));
            _mm256_storeu_si256((__m256i *) (even + i), _mm256_permute4x64_epi64(_mm256_packus_epi32(_mm256_and_si256(a, WORD), _mm256_and_si256(b, WORD)), 0xD8));
            _mm256_storeu_si256((__m256i *) (odd + i), _mm256_permute4x64_epi64(_mm256_packus_epi32(_mm256_srli_epi32(a, 16), _mm256_srli_epi32(b, 16)), 0xD8));
        }
    }
    float layerY(const TableLayer &L, int octave, int16_t *out, int &low) {
        static const float GY[16] = {1, 1, -1, -1, 0, 0, 0, 0, 1, -1, 1, -1, 1, -1, 1, -1};
        const int step = octave ? 4 : 2, sub = octave ? 2 : 1, base = octave ? 6 : 4, taps = base + 3 * sub, period = 256 / step, planes = L.planes; const float kernelScale = octave ? 4096.0f : 8192.0f;
        float kxa[12] = {0}, kza[12] = {0}, size = 1e-6f; __m256i kx[12];
        for (int j = 0; j < 4; j++) for (int r = 0; r < base; r++) { kxa[r + sub * j] += L.kxA[r]; kza[r + sub * j] += L.kzA[r]; }
        for (int r = 0; r < taps; r++) kx[r] = _mm256_set1_epi16((int16_t) std::lrint(std::min(kxa[r], 3.99f) * kernelScale));
        for (int k = 0; k < planes; k++) size += std::fabs(L.wyB[k]);
        const float scale = 8000.0f / size, entry = scale * (octave ? 0.125f : 0.25f);
        for (int j = 0; j < 272; j++) gy[j] = GY[L.d[j & 255] & 15];
        for (int j = 0; j < 256; j += 8) { // the z filter, once
            __m256 sy = _mm256_setzero_ps();
            for (int r = 0; r < taps; r++) sy = _mm256_fmadd_ps(_mm256_set1_ps(kza[r]), _mm256_loadu_ps(gy + j + r), sy);
            _mm256_store_ps(ax + j, sy);
        }
        for (int k = 0; k < planes; k++) {
            const __m256 w = _mm256_set1_ps(L.wyB[k] * entry);
            for (int j = 0; j < 256; j += 16) {
                const __m256i a = _mm256_cvtps_epi32(_mm256_mul_ps(w, _mm256_load_ps(ax + j))), b = _mm256_cvtps_epi32(_mm256_mul_ps(w, _mm256_load_ps(ax + j + 8)));
                _mm256_store_si256((__m256i *) (full16 + j), _mm256_permute4x64_epi64(_mm256_packs_epi32(a, b), 0xD8));
            }
            if (step == 2) split16(full16, 128, table16[k][0], table16[k][1]);
            else { split16(full16, 128, half16[0], half16[1]); split16(half16[0], 64, table16[k][0], table16[k][2]); split16(half16[1], 64, table16[k][1], table16[k][3]); }
            for (int phase = 0; phase < step; phase++) memcpy(table16[k][phase] + period, table16[k][phase], period * sizeof(int16_t));
        }
        __m256i lowest = _mm256_set1_epi16(32767); int have = 0;
        for (int m = 0; m < period; m++) {
            for (; have < step * m + taps; have++) {
                const uint8_t dx = L.d[(L.baseX + have) & 255]; int16_t *Y = ring16[have & (RING - 1)]; const int16_t *src[4];
                for (int k = 0; k < planes; k++) { const int e = (L.d[(uint8_t) (dx + (uint8_t) (L.y0 + k))] + L.baseZ) & 255; src[k] = table16[k][e % step] + e / step; }
                for (int t = 0; t < period; t += 16) {
                    __m256i y = _mm256_loadu_si256((const __m256i *) (src[0] + t));
                    for (int k = 1; k < planes; k++) y = _mm256_adds_epi16(y, _mm256_loadu_si256((const __m256i *) (src[k] + t)));
                    _mm256_store_si256((__m256i *) (Y + t), y);
                }
            }
            const int16_t *Y[12]; for (int r = 0; r < taps; r++) Y[r] = ring16[(step * m + r) & (RING - 1)];
            for (int t = 0; t < period; t += 16) {
                __m256i sum = _mm256_mulhrs_epi16(_mm256_load_si256((const __m256i *) (Y[0] + t)), kx[0]);
                for (int r = 1; r < taps; r++) sum = _mm256_adds_epi16(sum, _mm256_mulhrs_epi16(_mm256_load_si256((const __m256i *) (Y[r] + t)), kx[r]));
                lowest = _mm256_min_epi16(lowest, sum); _mm256_storeu_si256((__m256i *) (out + m * period + t), sum);
            }
        }
        alignas(32) int16_t lanes[16]; _mm256_store_si256((__m256i *) lanes, lowest); low = lanes[0];
        for (int i = 1; i < 16; i++) low = std::min(low, (int) lanes[i]);
        return (octave ? 64.0f : 16.0f) / scale * L.norm * 0.0625f;
    }
    float low8y(const TableSeed &seed) { int a, b; unit8[0] = layerY(seed.layer[0], 0, windows8[0], a); unit8[1] = layerY(seed.layer[2], 0, windows8[1], b); return a * unit8[0] + b * unit8[1]; }
    // the same with octave -7 added (y components, 16-bit); after low8y() of the same seed
    float lowBothY(const TableSeed &seed) {
        float total = 0;
        for (int stack = 0; stack < 2; stack++) {
            int low; const float u7 = layerY(seed.layer[2 * stack + 1], 1, windows7, low), u8 = unit8[stack]; const __m256 a8 = _mm256_set1_ps(u8), a7 = _mm256_set1_ps(u7); __m256 lowest = _mm256_set1_ps(1e9f);
            for (int m = 0; m < 128; m++) {
                const int16_t *a = windows8[stack] + m * 128, *b = windows7 + (m & 63) * 64;
                for (int t = 0; t < 64; t += 8) {
                    const __m256 v7 = _mm256_mul_ps(_mm256_cvtepi32_ps(_mm256_cvtepi16_epi32(_mm_loadu_si128((const __m128i *) (b + t)))), a7);
                    lowest = _mm256_min_ps(lowest, _mm256_fmadd_ps(_mm256_cvtepi32_ps(_mm256_cvtepi16_epi32(_mm_loadu_si128((const __m128i *) (a + t)))), a8, v7));
                    lowest = _mm256_min_ps(lowest, _mm256_fmadd_ps(_mm256_cvtepi32_ps(_mm256_cvtepi16_epi32(_mm_loadu_si128((const __m128i *) (a + t + 64)))), a8, v7));
                }
            }
            alignas(32) float lanes[8]; _mm256_store_ps(lanes, lowest); float low2 = lanes[0];
            for (int i = 1; i < 8; i++) low2 = std::min(low2, lanes[i]);
            total += low2;
        }
        return total;
    }
    float low8(const TableSeed &seed) { return layer(seed.layer[0], 0, field8[0]) + layer(seed.layer[2], 0, field8[1]); }
    float lowBoth(const TableSeed &seed) {
        float total = 0;
        for (int stack = 0; stack < 2; stack++) {
            layer(seed.layer[2 * stack + 1], 1, field7); float lowest = 1e9f; const float *f8 = field8[stack];
            for (int m = 0; m < 128; m++) { const float *a = f8 + m * 128, *b = field7 + (m & 63) * 64; for (int t = 0; t < 64; t++) lowest = std::min(lowest, std::min(a[t] + b[t], a[t + 64] + b[t])); }
            total += lowest;
        }
        return total;
    }
};
