// host side of gputables.cu: a seed's four lowest cave_cheese layers with the weights of analyticgate.hpp, for windows
// of 512 blocks that start a whole octave -8 cell apart (256 blocks: every second window of the CPU gate)
#pragma once
#include "analyticgate.hpp"
#include "gpuscan.h"
#include <cstring>
// one layer's part: octave 0 is -8 (windows of 2 cells), 1 is -7 (4 cells)
static inline void tableLayer(const NoiseLayer &layer, int octave, TableLayer &L) {
    const int cells = octave ? 2 : 1, piece = 2 * cells, taps = piece + 2; const PerlinNoise &p = layer.perlin;
    memcpy(L.d, p.d, 256);
    double yLo = AnalyticGate::Y_LO * 0.6666666666666666 * layer.frequency + p.b, yHi = AnalyticGate::Y_HI * 0.6666666666666666 * layer.frequency + p.b, wa[8], wb[8];
    L.y0 = AnalyticGate::weights(yLo, yHi, wa, wb, 4); L.planes = (int) std::floor(yHi) - L.y0 + 2;
    for (int k = 0; k < 4; k++) { L.wyA[k] = (float) wa[k]; L.wyB[k] = (float) wb[k]; }
    L.baseX = AnalyticGate::weights(p.a, p.a + piece, wa, wb, taps);
    for (int r = 0; r < 6; r++) { L.kxA[r] = r < taps ? (float) wa[r] : 0; L.kxB[r] = r < taps ? (float) wb[r] : 0; }
    L.baseZ = AnalyticGate::weights(p.c, p.c + piece, wa, wb, taps);
    for (int r = 0; r < 6; r++) { L.kzA[r] = r < taps ? (float) wa[r] : 0; L.kzB[r] = r < taps ? (float) wb[r] : 0; }
    L.norm = (float) (layer.amplitude / ((double) piece * piece * (yHi - yLo)));
}
static inline void tableSeed(const NormalNoise &cheese, TableSeed &out) {
    for (int li = 0; li < 4; li++) tableLayer(cheese.layers[2 * (li % 2) + li / 2], li % 2, out.layer[li]);
}

// The two layers of one octave alone (octave 0: -8, into out.layer[0] and [2]; 1: -7, into [1] and [3]). The first look
// needs octave -8 only and stops six seeds in seven, so the octave -7 tables are shuffled for the rest alone.
static inline void tableSeedOctave(uint64_t worldSeed, int octave, TableSeed &out) {
    static const NormalNoise pattern = makeNoise(0, "minecraft:cave_cheese");
    static const NameHash cheeseHash = nameHash("minecraft:cave_cheese"), octaveHash[2] = {nameHash("octave_-8"), nameHash("octave_-7")};
    Xoroshiro world; xSetSeed(&world, worldSeed);
    uint64_t worldLo = xNextLong(&world), worldHi = xNextLong(&world);
    Xoroshiro random = {worldLo ^ cheeseHash.lo, worldHi ^ cheeseHash.hi};
    for (int half = 0; half < 2; half++) {
        uint64_t forkLo = xNextLong(&random), forkHi = xNextLong(&random);
        NoiseLayer layer; Xoroshiro octaveRandom = {forkLo ^ octaveHash[octave].lo, forkHi ^ octaveHash[octave].hi};
        xPerlinInit(&layer.perlin, &octaveRandom); layer.frequency = pattern.layers[2 * octave + half].frequency; layer.amplitude = pattern.layers[2 * octave + half].amplitude;
        tableLayer(layer, octave, out.layer[2 * half + octave]);
    }
}

// The same from the seed alone, making only the four layers that are needed (makeNoise builds all 14 of cave_cheese:
// three times the work). The layers' amplitudes and frequencies do not depend on the seed.
static inline void tableSeed(uint64_t worldSeed, TableSeed &out) {
    static const NormalNoise pattern = makeNoise(0, "minecraft:cave_cheese");
    static const NameHash cheeseHash = nameHash("minecraft:cave_cheese"), octaveHash[2] = {nameHash("octave_-8"), nameHash("octave_-7")};
    Xoroshiro world; xSetSeed(&world, worldSeed);
    uint64_t worldLo = xNextLong(&world), worldHi = xNextLong(&world);
    Xoroshiro random = {worldLo ^ cheeseHash.lo, worldHi ^ cheeseHash.hi};
    NormalNoise four; four.layers.resize(4);
    for (int half = 0; half < 2; half++) {
        uint64_t forkLo = xNextLong(&random), forkHi = xNextLong(&random);
        for (int octave = 0; octave < 2; octave++) {
            NoiseLayer &layer = four.layers[2 * octave + half]; Xoroshiro octaveRandom = {forkLo ^ octaveHash[octave].lo, forkHi ^ octaveHash[octave].hi};
            xPerlinInit(&layer.perlin, &octaveRandom); layer.frequency = pattern.layers[2 * octave + half].frequency; layer.amplitude = pattern.layers[2 * octave + half].amplitude;
        }
    }
    tableSeed(four, out);
}

// For the GPU's first coarse look (gpulook.cu): octave -8 of each stack alone, with the y weights of the gradients' y
// components and the 7-tap kernels of the tapered 1280-block window (four 512-block windows a cell apart). Half the
// work of tableSeed(): the octave -7 tables are not shuffled.
static inline void lookSeed(uint64_t worldSeed, LookSeed &out) {
    static const NormalNoise pattern = makeNoise(0, "minecraft:cave_cheese");
    static const NameHash cheeseHash = nameHash("minecraft:cave_cheese"), o8 = nameHash("octave_-8");
    Xoroshiro world; xSetSeed(&world, worldSeed);
    uint64_t worldLo = xNextLong(&world), worldHi = xNextLong(&world);
    Xoroshiro random = {worldLo ^ cheeseHash.lo, worldHi ^ cheeseHash.hi};
    for (int half = 0; half < 2; half++) {
        uint64_t forkLo = xNextLong(&random), forkHi = xNextLong(&random);
        Xoroshiro octaveRandom = {forkLo ^ o8.lo, forkHi ^ o8.hi}; PerlinNoise p; xPerlinInit(&p, &octaveRandom);
        const double frequency = pattern.layers[half].frequency, amplitude = pattern.layers[half].amplitude; LookLayer &L = out.layer[half];
        memcpy(L.d, p.d, 256);
        double yLo = AnalyticGate::Y_LO * 0.6666666666666666 * frequency + p.b, yHi = AnalyticGate::Y_HI * 0.6666666666666666 * frequency + p.b, wa[8], wb[8];
        L.y0 = AnalyticGate::weights(yLo, yHi, wa, wb, 4); L.planes = (int) std::floor(yHi) - L.y0 + 2;
        for (int k = 0; k < 4; k++) L.wyB[k] = (float) wb[k];
        for (int r = 0; r < 7; r++) L.kx[r] = L.kz[r] = 0;
        L.baseX = AnalyticGate::weights(p.a, p.a + 2, wa, wb, 4);
        for (int j = 0; j < 4; j++) for (int r = 0; r < 4; r++) L.kx[r + j] += (float) wa[r];
        L.baseZ = AnalyticGate::weights(p.c, p.c + 2, wa, wb, 4);
        for (int j = 0; j < 4; j++) for (int r = 0; r < 4; r++) L.kz[r + j] += (float) wa[r];
        L.norm = (float) (amplitude / (4.0 * (yHi - yLo)));
    }
}

