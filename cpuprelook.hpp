// The cheap looks of the seed gate on a CPU thread (AVX2): the same numbers as the prelook kernel of gputables.cu, so
// that CPU threads can look at seeds next to the GPU, plus a second number the GPU does not make.
//   low8():    octave -8 alone: the lowest 1280-block window mean (tapered: 4 x 4 of the 512-block windows, a cell
//              apart) of stack A plus that of stack B, windows every 512 blocks (128 x 128 per stack)
//   lowBoth(): the same with octave -7 added to each stack's field (its windows repeat after 64, the table being 256
//              cells of 128 blocks); call it after low8() of the same seed
// Per layer: the gradient sums down y of a lattice line (x, every z) go into a ring of lines, each made once, 8 columns
// at a time (the gradient components of a table byte by byte shuffle); a window's row along x is a weighted sum of its
// lines; the windows along z are weighted sums of that row, read at a stride (split by phase so the sums run on
// neighbours). The columns are stored turned by the layer's base z, so no index wraps but at the end of a row.
#pragma once
#include "tableseed.hpp"
#include <immintrin.h>
struct CoarseLook {
    static const int RING = 16;
    alignas(64) float ring[3][RING][256];
    alignas(64) float rowXY[288], rowZ[288], phase[2][4][144], half[2][2][144];
    alignas(64) float field8[2][128 * 128], field7[64 * 64];
    uint8_t d2[544];
    static inline void split(const float *in, int pairs, float *even, float *odd) { // in[2 i] and in[2 i + 1] apart, pairs a multiple of 8
        for (int i = 0; i < pairs; i += 8) {
            __m256 a = _mm256_loadu_ps(in + 2 * i), b = _mm256_loadu_ps(in + 2 * i + 8);
            _mm256_storeu_ps(even + i, _mm256_castpd_ps(_mm256_permute4x64_pd(_mm256_castps_pd(_mm256_shuffle_ps(a, b, 0x88)), 0xD8)));
            _mm256_storeu_ps(odd + i, _mm256_castpd_ps(_mm256_permute4x64_pd(_mm256_castps_pd(_mm256_shuffle_ps(a, b, 0xDD)), 0xD8)));
        }
    }
    inline void line(const TableLayer &L, int i) {
        static const __m128i GX = _mm_setr_epi8(1, -1, 1, -1, 1, -1, 1, -1, 0, 0, 0, 0, 1, 0, -1, 0), GY = _mm_setr_epi8(1, 1, -1, -1, 0, 0, 0, 0, 1, -1, 1, -1, 1, -1, 1, -1), GZ = _mm_setr_epi8(0, 0, 0, 0, 1, 1, -1, -1, 1, 1, -1, -1, 0, 1, 0, -1), LOW = _mm_set1_epi8(15);
        const uint8_t dx = L.d[(L.baseX + i) & 255]; const int slot = i & (RING - 1), planes = L.planes;
        float *X = ring[0][slot], *Y = ring[1][slot], *Z = ring[2][slot]; const uint8_t *src[4]; __m256 wa[4], wb[4];
        for (int k = 0; k < planes; k++) { src[k] = d2 + ((L.d[(uint8_t) (dx + (uint8_t) (L.y0 + k))] + L.baseZ) & 255); wa[k] = _mm256_set1_ps(L.wyA[k]); wb[k] = _mm256_set1_ps(L.wyB[k]); }
        for (int t = 0; t < 256; t += 8) {
            __m256 sx = _mm256_setzero_ps(), sy = sx, sz = sx;
            for (int k = 0; k < planes; k++) {
                const __m128i h = _mm_and_si128(_mm_loadl_epi64((const __m128i *) (src[k] + t)), LOW);
                sx = _mm256_fmadd_ps(_mm256_cvtepi32_ps(_mm256_cvtepi8_epi32(_mm_shuffle_epi8(GX, h))), wa[k], sx);
                sy = _mm256_fmadd_ps(_mm256_cvtepi32_ps(_mm256_cvtepi8_epi32(_mm_shuffle_epi8(GY, h))), wb[k], sy);
                sz = _mm256_fmadd_ps(_mm256_cvtepi32_ps(_mm256_cvtepi8_epi32(_mm_shuffle_epi8(GZ, h))), wa[k], sz);
            }
            _mm256_store_ps(X + t, sx); _mm256_store_ps(Y + t, sy); _mm256_store_ps(Z + t, sz);
        }
    }
    // the window means of one layer on the coarse grid into out[m * period + t] (period 128 for octave -8, 64 for -7); returns the lowest
    float layer(const TableLayer &L, int octave, float *out) {
        const int step = octave ? 4 : 2, sub = octave ? 2 : 1, base = octave ? 6 : 4, taps = base + 3 * sub, period = 256 / step;
        float kxa[12] = {0}, kxb[12] = {0}, kza[12] = {0}, kzb[12] = {0};
        for (int j = 0; j < 4; j++) for (int r = 0; r < base; r++) { kxa[r + sub * j] += L.kxA[r]; kxb[r + sub * j] += L.kxB[r]; kza[r + sub * j] += L.kzA[r]; kzb[r + sub * j] += L.kzB[r]; }
        memcpy(d2, L.d, 256); memcpy(d2 + 256, L.d, 256); memcpy(d2 + 512, L.d, 32);
        const float norm = L.norm * 0.0625f; int have = 0; float lowest = 1e9f;
        for (int m = 0; m < period; m++) {
            for (; have < step * m + taps; have++) line(L, have);
            for (int t = 0; t < 256; t++) rowXY[t] = rowZ[t] = 0;
            for (int r = 0; r < taps; r++) {
                const int slot = (step * m + r) & (RING - 1); const float *X = ring[0][slot], *Y = ring[1][slot], *Z = ring[2][slot]; const float a = kxb[r], b = kxa[r];
                for (int t = 0; t < 256; t++) { rowXY[t] += a * X[t] + b * Y[t]; rowZ[t] += b * Z[t]; }
            }
            for (int t = 0; t < 32; t++) { rowXY[256 + t] = rowXY[t]; rowZ[256 + t] = rowZ[t]; }
            const float *row[2] = {rowXY, rowZ};
            for (int a = 0; a < 2; a++) {
                if (step == 2) split(row[a], 144, phase[a][0], phase[a][1]);
                else { split(row[a], 144, half[a][0], half[a][1]); split(half[a][0], 72, phase[a][0], phase[a][2]); split(half[a][1], 72, phase[a][1], phase[a][3]); }
            }
            float *o = out + m * period;
            for (int t = 0; t < period; t++) o[t] = 0;
            for (int r = 0; r < taps; r++) {
                const float *P = phase[0][r % step] + r / step, *Q = phase[1][r % step] + r / step; const float a = kza[r], b = kzb[r];
                for (int t = 0; t < period; t++) o[t] += a * P[t] + b * Q[t];
            }
            for (int t = 0; t < period; t++) { o[t] *= norm; lowest = std::min(lowest, o[t]); }
        }
        return lowest;
    }
    // The first number: octave -8 from the y components of the gradients alone (the x and z ones only count at a window's
    // edges), in 16-bit fixed point all the way (32 columns per step in the lines, 16 after): it sorts seeds as well as
    // low8() does at a sixth of the time. Scales: a line holds sums of the y weights x `scale` (at most 8,000 in size),
    // a row is lines x kernel / 4 (at most 16,000), a window rows x kernel / 4 (at most 32,000); the sums saturate.
    alignas(64) int16_t ring16[RING][256], row16[288 + 32];
    inline void lineY(const TableLayer &L, int i, const __m256i *wb) {
        static const __m256i GY = _mm256_setr_epi8(1, 1, -1, -1, 0, 0, 0, 0, 1, -1, 1, -1, 1, -1, 1, -1, 1, 1, -1, -1, 0, 0, 0, 0, 1, -1, 1, -1, 1, -1, 1, -1), LOW = _mm256_set1_epi8(15);
        const uint8_t dx = L.d[(L.baseX + i) & 255]; const int planes = L.planes; int16_t *Y = ring16[i & (RING - 1)]; const uint8_t *src[4];
        for (int k = 0; k < planes; k++) src[k] = d2 + ((L.d[(uint8_t) (dx + (uint8_t) (L.y0 + k))] + L.baseZ) & 255);
        for (int t = 0; t < 256; t += 32) {
            __m256i lo = _mm256_setzero_si256(), hi = lo;
            for (int k = 0; k < planes; k++) {
                const __m256i g = _mm256_shuffle_epi8(GY, _mm256_and_si256(_mm256_loadu_si256((const __m256i *) (src[k] + t)), LOW));
                lo = _mm256_add_epi16(lo, _mm256_sign_epi16(wb[k], _mm256_cvtepi8_epi16(_mm256_castsi256_si128(g)))); hi = _mm256_add_epi16(hi, _mm256_sign_epi16(wb[k], _mm256_cvtepi8_epi16(_mm256_extracti128_si256(g, 1))));
            }
            _mm256_store_si256((__m256i *) (Y + t), lo); _mm256_store_si256((__m256i *) (Y + t + 16), hi);
        }
    }
    // the windows of one layer from the y components, 16-bit, into out[m * period + t] (128 x 128 for octave -8, 64 x 64 for
    // octave -7, whose kernels are twice as long: its rows and windows are sums / 8); returns what one unit is worth
    alignas(64) int16_t windows8[2][128 * 128], windows7[64 * 64], half16[2][144], phase16[4][144]; float unit8[2];
    float layerY(const TableLayer &L, int octave, int16_t *out, int &low) {
        const int step = octave ? 4 : 2, sub = octave ? 2 : 1, base = octave ? 6 : 4, taps = base + 3 * sub, period = 256 / step; const float kernelScale = octave ? 4096.0f : 8192.0f;
        float kxa[12] = {0}, kza[12] = {0}, size = 1e-6f; __m256i kx[12], kz[12], wb[4];
        for (int j = 0; j < 4; j++) for (int r = 0; r < base; r++) { kxa[r + sub * j] += L.kxA[r]; kza[r + sub * j] += L.kzA[r]; }
        for (int r = 0; r < taps; r++) { kx[r] = _mm256_set1_epi16((int16_t) std::lrint(std::min(kxa[r], 3.99f) * kernelScale)); kz[r] = _mm256_set1_epi16((int16_t) std::lrint(std::min(kza[r], 3.99f) * kernelScale)); }
        for (int k = 0; k < L.planes; k++) size += std::fabs(L.wyB[k]);
        const float scale = 8000.0f / size;
        for (int k = 0; k < L.planes; k++) wb[k] = _mm256_set1_epi16((int16_t) std::lrint(L.wyB[k] * scale));
        memcpy(d2, L.d, 256); memcpy(d2 + 256, L.d, 256); memcpy(d2 + 512, L.d, 32);
        const __m256i WORD = _mm256_set1_epi32(0xFFFF); __m256i lowest = _mm256_set1_epi16(32767); int have = 0;
        auto split16 = [&](const int16_t *in, int pairs, int16_t *even, int16_t *odd) {
            for (int i = 0; i < pairs; i += 16) {
                const __m256i a = _mm256_loadu_si256((const __m256i *) (in + 2 * i)), b = _mm256_loadu_si256((const __m256i *) (in + 2 * i + 16));
                _mm256_storeu_si256((__m256i *) (even + i), _mm256_permute4x64_epi64(_mm256_packus_epi32(_mm256_and_si256(a, WORD), _mm256_and_si256(b, WORD)), 0xD8));
                _mm256_storeu_si256((__m256i *) (odd + i), _mm256_permute4x64_epi64(_mm256_packus_epi32(_mm256_srli_epi32(a, 16), _mm256_srli_epi32(b, 16)), 0xD8));
            }
        };
        for (int m = 0; m < period; m++) {
            for (; have < step * m + taps; have++) lineY(L, have, wb);
            const int16_t *Y[12]; for (int r = 0; r < taps; r++) Y[r] = ring16[(step * m + r) & (RING - 1)];
            for (int t = 0; t < 256; t += 16) {
                __m256i sum = _mm256_mulhrs_epi16(_mm256_load_si256((const __m256i *) (Y[0] + t)), kx[0]);
                for (int r = 1; r < taps; r++) sum = _mm256_adds_epi16(sum, _mm256_mulhrs_epi16(_mm256_load_si256((const __m256i *) (Y[r] + t)), kx[r]));
                _mm256_store_si256((__m256i *) (row16 + t), sum);
            }
            _mm256_store_si256((__m256i *) (row16 + 256), _mm256_load_si256((const __m256i *) row16)); _mm256_store_si256((__m256i *) (row16 + 272), _mm256_load_si256((const __m256i *) (row16 + 16)));
            if (step == 2) split16(row16, 144, phase16[0], phase16[1]); // the row's columns by phase of the stride
            else { split16(row16, 144, half16[0], half16[1]); split16(half16[0], 64 + 16, phase16[0], phase16[2]); split16(half16[1], 64 + 16, phase16[1], phase16[3]); }
            for (int t = 0; t < period; t += 16) {
                __m256i sum = _mm256_mulhrs_epi16(_mm256_loadu_si256((const __m256i *) (phase16[0] + t)), kz[0]);
                for (int r = 1; r < taps; r++) sum = _mm256_adds_epi16(sum, _mm256_mulhrs_epi16(_mm256_loadu_si256((const __m256i *) (phase16[r % step] + r / step + t)), kz[r]));
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
