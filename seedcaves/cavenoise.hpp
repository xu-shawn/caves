// The game's NormalNoise (Minecraft 26.3) on top of cubiomes' Perlin noise: the same seeding, octaves and value factors as
// net.minecraft.world.level.levelgen.synth.NormalNoise.create, checked against the game with java/NoiseProbe.
#pragma once
extern "C" {
#include "noise.h"
}
#include <climits>
#include <cmath>
#include <cstdio>
#include <vector>
#include "name_hashes.h"
#include "noise_params.h"

// cubiomes' samplePerlin(noise, x, y, z, 0, 0) with the eight gradient switches (which the branch predictor cannot
// follow) replaced by table lookups: the same value up to rounding, at a third of the time.
static inline double fastPerlin(const PerlinNoise *noise, double d1, double d2, double d3) {
    static const double GX[16] = {1, -1, 1, -1, 1, -1, 1, -1, 0, 0, 0, 0, 1, 0, -1, 0}, GY[16] = {1, 1, -1, -1, 0, 0, 0, 0, 1, -1, 1, -1, 1, -1, 1, -1}, GZ[16] = {0, 0, 0, 0, 1, 1, -1, -1, 1, 1, -1, -1, 0, 1, 0, -1};
    d1 += noise->a; d2 += noise->b; d3 += noise->c;
    double i1 = std::floor(d1), i2 = std::floor(d2), i3 = std::floor(d3);
    d1 -= i1; d2 -= i2; d3 -= i3;
    uint8_t h1 = (uint8_t) (int) i1, h2 = (uint8_t) (int) i2, h3 = (uint8_t) (int) i3;
    double t1 = d1 * d1 * d1 * (d1 * (d1 * 6.0 - 15.0) + 10.0), t2 = d2 * d2 * d2 * (d2 * (d2 * 6.0 - 15.0) + 10.0), t3 = d3 * d3 * d3 * (d3 * (d3 * 6.0 - 15.0) + 10.0);
    const uint8_t *idx = noise->d;
    uint8_t a1 = (uint8_t) (idx[h1] + h2), b1 = (uint8_t) (idx[h1 + 1] + h2);
    uint8_t a2 = (uint8_t) (idx[a1] + h3), a3 = (uint8_t) (idx[a1 + 1] + h3), b2 = (uint8_t) (idx[b1] + h3), b3 = (uint8_t) (idx[b1 + 1] + h3);
    auto g = [&](uint8_t i, double x, double y, double z) { i &= 15; return GX[i] * x + GY[i] * y + GZ[i] * z; };
    double l1 = g(idx[a2], d1, d2, d3), l2 = g(idx[b2], d1 - 1, d2, d3), l3 = g(idx[a3], d1, d2 - 1, d3), l4 = g(idx[b3], d1 - 1, d2 - 1, d3);
    double l5 = g(idx[a2 + 1], d1, d2, d3 - 1), l6 = g(idx[b2 + 1], d1 - 1, d2, d3 - 1), l7 = g(idx[a3 + 1], d1, d2 - 1, d3 - 1), l8 = g(idx[b3 + 1], d1 - 1, d2 - 1, d3 - 1);
    l1 += t1 * (l2 - l1); l3 += t1 * (l4 - l3); l5 += t1 * (l6 - l5); l7 += t1 * (l8 - l7);
    l1 += t2 * (l3 - l1); l5 += t2 * (l7 - l5);
    return l1 + t3 * (l5 - l1);
}

// The same noise down a column: x and z fixed, many heights. Within a lattice cell the value is
//     (1 - t(v)) (P0 + Q0 v) + t(v) (P1 + Q1 (v - 1)),      v = the height inside the cell, t the fade,
// where P and Q are the x-z interpolations of the corner gradients' horizontal part and of their y component, for the
// cell's bottom (0) and top (1) corners. So the 14 table lookups are done once per cell and each further height costs a
// dozen operations. Values equal fastPerlin's up to rounding.
struct ColumnPerlin {
    const PerlinNoise *noise; double d1, d3, t1, t3, P0, Q0, P1, Q1; uint8_t h1, h3; long cell;
    ColumnPerlin(const PerlinNoise *n, double x, double z) : noise(n), cell(LONG_MIN) {
        x += n->a; z += n->c; double i1 = std::floor(x), i3 = std::floor(z);
        d1 = x - i1; d3 = z - i3; h1 = (uint8_t) (int) i1; h3 = (uint8_t) (int) i3;
        t1 = d1 * d1 * d1 * (d1 * (d1 * 6.0 - 15.0) + 10.0); t3 = d3 * d3 * d3 * (d3 * (d3 * 6.0 - 15.0) + 10.0);
    }
    double at(double y) {
        static const double GX[16] = {1, -1, 1, -1, 1, -1, 1, -1, 0, 0, 0, 0, 1, 0, -1, 0}, GY[16] = {1, 1, -1, -1, 0, 0, 0, 0, 1, -1, 1, -1, 1, -1, 1, -1}, GZ[16] = {0, 0, 0, 0, 1, 1, -1, -1, 1, 1, -1, -1, 0, 1, 0, -1};
        y += noise->b; double i2 = std::floor(y), d2 = y - i2;
        if ((long) i2 != cell) {
            cell = (long) i2; const uint8_t *idx = noise->d; uint8_t h2 = (uint8_t) (int) i2;
            uint8_t a1 = (uint8_t) (idx[h1] + h2), b1 = (uint8_t) (idx[h1 + 1] + h2);
            uint8_t a2 = (uint8_t) (idx[a1] + h3), a3 = (uint8_t) (idx[a1 + 1] + h3), b2 = (uint8_t) (idx[b1] + h3), b3 = (uint8_t) (idx[b1 + 1] + h3);
            // corners: (x0 or x1, z0 or z1) of the bottom plane, then of the top plane
            const uint8_t g[2][4] = {{(uint8_t) (idx[a2] & 15), (uint8_t) (idx[b2] & 15), (uint8_t) (idx[a2 + 1] & 15), (uint8_t) (idx[b2 + 1] & 15)},
                                     {(uint8_t) (idx[a3] & 15), (uint8_t) (idx[b3] & 15), (uint8_t) (idx[a3 + 1] & 15), (uint8_t) (idx[b3 + 1] & 15)}};
            double P[2], Q[2];
            for (int j = 0; j < 2; j++) {
                double p00 = GX[g[j][0]] * d1 + GZ[g[j][0]] * d3, p10 = GX[g[j][1]] * (d1 - 1) + GZ[g[j][1]] * d3, p01 = GX[g[j][2]] * d1 + GZ[g[j][2]] * (d3 - 1), p11 = GX[g[j][3]] * (d1 - 1) + GZ[g[j][3]] * (d3 - 1);
                double pz0 = p00 + t1 * (p10 - p00), pz1 = p01 + t1 * (p11 - p01); P[j] = pz0 + t3 * (pz1 - pz0);
                double q0 = GY[g[j][0]] + t1 * (GY[g[j][1]] - GY[g[j][0]]), q1 = GY[g[j][2]] + t1 * (GY[g[j][3]] - GY[g[j][2]]); Q[j] = q0 + t3 * (q1 - q0);
            }
            P0 = P[0]; Q0 = Q[0]; P1 = P[1]; Q1 = Q[1];
        }
        double t2 = d2 * d2 * d2 * (d2 * (d2 * 6.0 - 15.0) + 10.0), low = P0 + Q0 * d2, high = P1 + Q1 * (d2 - 1);
        return low + t2 * (high - low);
    }
};

struct NoiseLayer {
    PerlinNoise perlin;
    double frequency;
    float amplitude;
};

struct NormalNoise {
    std::vector<NoiseLayer> layers; // the first and the second Perlin noise of each octave, lowest octave first

    float sample(double x, double y, double z) const {
        float value = 0;
        for (const NoiseLayer &layer : layers) value += layer.amplitude * (float) fastPerlin(&layer.perlin, x * layer.frequency, y * layer.frequency, z * layer.frequency);
        return value;
    }
    // only the octaves below a cutoff (the layers come in pairs)
    float sampleOctaves(double x, double y, double z, int octaves) const {
        float value = 0;
        for (int i = 0; i < 2 * octaves && i < (int) layers.size(); i++) {
            const NoiseLayer &layer = layers[i];
            value += layer.amplitude * (float) fastPerlin(&layer.perlin, x * layer.frequency, y * layer.frequency, z * layer.frequency);
        }
        return value;
    }
};

static inline const NoiseParams &noiseParams(const char *name) {
    for (const NoiseParams &p : NOISE_PARAMS) if (!strcmp(p.name, name)) return p;
    __builtin_trap();
}

static inline NormalNoise makeNoise(uint64_t worldSeed, const char *name) {
    const NoiseParams &params = noiseParams(name);
    // RandomState: new XoroshiroRandomSource(seed).forkPositional().fromHashOf(name)
    Xoroshiro world; xSetSeed(&world, worldSeed);
    uint64_t worldLo = xNextLong(&world), worldHi = xNextLong(&world);
    const NameHash &hash = nameHash(name);
    Xoroshiro random = {worldLo ^ hash.lo, worldHi ^ hash.hi};
    // NormalNoise.create: two positional forks, a Perlin noise per octave from each
    uint64_t forkLo[2], forkHi[2];
    for (int half = 0; half < 2; half++) { forkLo[half] = xNextLong(&random); forkHi[half] = xNextLong(&random); }
    // buildOctaves and computeNormalizationFactor
    int n = params.octaveCount;
    double amplitude = params.baseAmplitude * (std::pow(0.5, -(n - 1)) / (std::pow(0.5, -n) - 1.0)), frequency = std::pow(2.0, params.baseOctave);
    std::vector<double> amplitudes, frequencies; std::vector<int> octaves;
    double target = 0, variance = 0;
    for (int i = 0; i < n; i++) {
        if (params.modifiers[i] != 0.0) {
            double modified = amplitude * params.modifiers[i];
            amplitudes.push_back(modified); frequencies.push_back(frequency); octaves.push_back(params.baseOctave + i);
            target += std::fabs(modified); variance += (0.2702247831245211 * std::fabs(modified)) * (0.2702247831245211 * std::fabs(modified));
        }
        frequency *= 2.0; amplitude *= 0.5;
    }
    double normalization = (target * 0.3333333333333333) / (std::sqrt(variance) * std::sqrt(2.0));
    NormalNoise noise;
    for (size_t i = 0; i < octaves.size(); i++) {
        char octaveName[32]; snprintf(octaveName, sizeof octaveName, "octave_%d", octaves[i]);
        const NameHash &octaveHash = nameHash(octaveName);
        for (int half = 0; half < 2; half++) {
            NoiseLayer layer;
            Xoroshiro octaveRandom = {forkLo[half] ^ octaveHash.lo, forkHi[half] ^ octaveHash.hi};
            xPerlinInit(&layer.perlin, &octaveRandom);
            layer.frequency = frequencies[i] * (half ? 1.0181268882175227 : 1.0);
            layer.amplitude = (float) (normalization * amplitudes[i]);
            noise.layers.push_back(layer);
        }
    }
    return noise;
}
