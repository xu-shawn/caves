// fastnoise.hpp: cubiomes' Perlin sampling without its switch per lattice corner (a table of gradient
// vectors instead), on cubiomes' own PerlinNoise structures. The arithmetic is cubiomes' operation for operation, so the
// values are the same doubles (fastcheck compares them); build with -ffp-contract=off like cubiomes itself.
#pragma once
extern "C" {
#include "noise.h"
}
#include <cmath>

static const double FAST_GRAD[16][3] = {   // indexedLerp(h, a, b, c) = g[0] a + g[1] b + g[2] c
    {1, 1, 0}, {-1, 1, 0}, {1, -1, 0}, {-1, -1, 0}, {1, 0, 1}, {-1, 0, 1}, {1, 0, -1}, {-1, 0, -1},
    {0, 1, 1}, {0, -1, 1}, {0, 1, -1}, {0, -1, -1}, {1, 1, 0}, {0, -1, 1}, {-1, 1, 0}, {0, -1, -1}};
static inline double fastGrad(uint8_t h, double a, double b, double c) { const double *g = FAST_GRAD[h & 15]; return g[0] * a + g[1] * b + g[2] * c; }
static inline double fastLerp(double part, double from, double to) { return from + part * (to - from); }

// samplePerlin(noise, d1, d2, d3, 0, 0); d2 == 0 takes the octave's own y as cubiomes does
static inline double fastPerlin(const PerlinNoise *noise, double d1, double d2, double d3) {
    uint8_t h1, h2, h3; double t1, t2, t3;
    if (d2 == 0.0) { d2 = noise->d2; h2 = noise->h2; t2 = noise->t2; }
    else { d2 += noise->b; const double i2 = floor(d2); d2 -= i2; h2 = (uint8_t) (int) i2; t2 = d2 * d2 * d2 * (d2 * (d2 * 6.0 - 15.0) + 10.0); }
    d1 += noise->a; d3 += noise->c;
    const double i1 = floor(d1), i3 = floor(d3);
    d1 -= i1; d3 -= i3;
    h1 = (uint8_t) (int) i1; h3 = (uint8_t) (int) i3;
    t1 = d1 * d1 * d1 * (d1 * (d1 * 6.0 - 15.0) + 10.0);
    t3 = d3 * d3 * d3 * (d3 * (d3 * 6.0 - 15.0) + 10.0);
    const uint8_t *idx = noise->d;
    const uint8_t a1 = (uint8_t) (idx[h1] + h2), b1 = (uint8_t) (idx[h1 + 1] + h2);
    const uint8_t a2 = (uint8_t) (idx[a1] + h3), a3 = (uint8_t) (idx[a1 + 1] + h3), b2 = (uint8_t) (idx[b1] + h3), b3 = (uint8_t) (idx[b1 + 1] + h3);
    double l1 = fastGrad(idx[a2], d1, d2, d3), l2 = fastGrad(idx[b2], d1 - 1, d2, d3);
    double l3 = fastGrad(idx[a3], d1, d2 - 1, d3), l4 = fastGrad(idx[b3], d1 - 1, d2 - 1, d3);
    double l5 = fastGrad(idx[a2 + 1], d1, d2, d3 - 1), l6 = fastGrad(idx[b2 + 1], d1 - 1, d2, d3 - 1);
    double l7 = fastGrad(idx[a3 + 1], d1, d2 - 1, d3 - 1), l8 = fastGrad(idx[b3 + 1], d1 - 1, d2 - 1, d3 - 1);
    l1 = fastLerp(t1, l1, l2); l3 = fastLerp(t1, l3, l4); l5 = fastLerp(t1, l5, l6); l7 = fastLerp(t1, l7, l8);
    l1 = fastLerp(t2, l1, l3); l5 = fastLerp(t2, l5, l7);
    return fastLerp(t3, l1, l5);
}
static inline double fastOctave(const OctaveNoise *noise, double x, double y, double z) {
    double v = 0;
    for (int i = 0; i < noise->octcnt; i++) { const PerlinNoise *p = noise->octaves + i; const double lf = p->lacunarity; v += p->amplitude * fastPerlin(p, x * lf, y * lf, z * lf); }
    return v;
}
static inline double fastDoublePerlin(const DoublePerlinNoise *noise, double x, double y, double z) {
    const double f = 337.0 / 331.0; double v = 0;
    v += fastOctave(&noise->octA, x, y, z);
    v += fastOctave(&noise->octB, x * f, y * f, z * f);
    return v * noise->amplitude;
}
