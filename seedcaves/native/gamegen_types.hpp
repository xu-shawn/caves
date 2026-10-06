// The game's noise types with the game's float arithmetic (Minecraft 26.3: synth/PerlinNoise, SmearedPerlinNoise,
// NoiseStack, NormalNoise, BlendedNoise), and the point context the generated density functions run in.
// Build everything that includes this with -ffp-contract=off: the game never fuses a multiply and an add.
#pragma once
extern "C" {
#include "noise.h"
}
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include "../name_hashes.h"
#include "../noise_params.h"

static inline int floorInt(double v) { int i = (int) v; return v < i ? i - 1 : i; }
static inline int floorIntF(float v) { int i = (int) v; return v < i ? i - 1 : i; }
static inline float lerpf(float a, float b, float c) { return b + a * (c - b); }
static inline float smoothstepf(float x) { return x * x * x * (x * (x * 6.0f - 15.0f) + 10.0f); }
static inline double wrapCoordinate(double x) {
    const double half = std::nextafter(1.6777216E7, 0.0);
    return x >= -half && x < half ? x : x - std::floor(x / 3.3554432E7 + 0.5) * 3.3554432E7;
}
static const int GRADIENTS[16][3] = {{1, 1, 0}, {-1, 1, 0}, {1, -1, 0}, {-1, -1, 0}, {1, 0, 1}, {-1, 0, 1}, {1, 0, -1}, {-1, 0, -1},
                                     {0, 1, 1}, {0, -1, 1}, {0, 1, -1}, {0, -1, -1}, {1, 1, 0}, {0, -1, 1}, {-1, 1, 0}, {0, -1, -1}};

struct GPerlin {
    uint8_t perm[256];
    double offsetX, offsetY, offsetZ;
    double fudgeYScale = 0; // only the smeared kind uses it

    void init(Xoroshiro *random) { // GradientNoise's constructor
        PerlinNoise noise; xPerlinInit(&noise, random);
        memcpy(perm, noise.d, 256); offsetX = noise.a; offsetY = noise.b; offsetZ = noise.c;
    }
    int permute(int x) const { return perm[x & 0xFF]; }
    static float gradDot(int hash, float x, float y, float z) { const int *g = GRADIENTS[hash & 15]; return g[0] * x + g[1] * y + g[2] * z; }
    float sampleAndLerp(int x, int y, int z, float rx, float ry, float rz, float originalY) const {
        int x0 = permute(x), x1 = permute(x + 1);
        int xy00 = permute(x0 + y), xy01 = permute(x0 + y + 1), xy10 = permute(x1 + y), xy11 = permute(x1 + y + 1);
        float d000 = gradDot(permute(xy00 + z), rx, ry, rz), d100 = gradDot(permute(xy10 + z), rx - 1.0f, ry, rz);
        float d010 = gradDot(permute(xy01 + z), rx, ry - 1.0f, rz), d110 = gradDot(permute(xy11 + z), rx - 1.0f, ry - 1.0f, rz);
        float d001 = gradDot(permute(xy00 + z + 1), rx, ry, rz - 1.0f), d101 = gradDot(permute(xy10 + z + 1), rx - 1.0f, ry, rz - 1.0f);
        float d011 = gradDot(permute(xy01 + z + 1), rx, ry - 1.0f, rz - 1.0f), d111 = gradDot(permute(xy11 + z + 1), rx - 1.0f, ry - 1.0f, rz - 1.0f);
        float ax = smoothstepf(rx), ay = smoothstepf(originalY), az = smoothstepf(rz);
        return lerpf(az, lerpf(ay, lerpf(ax, d000, d100), lerpf(ax, d010, d110)), lerpf(ay, lerpf(ax, d001, d101), lerpf(ax, d011, d111)));
    }
    float get(double _x, double _y, double _z) const { // PerlinNoise.get
        double x = wrapCoordinate(_x) + offsetX, y = wrapCoordinate(_y) + offsetY, z = wrapCoordinate(_z) + offsetZ;
        int fx = floorInt(x), fy = floorInt(y), fz = floorInt(z);
        float ry = (float) (y - fy);
        return sampleAndLerp(fx, fy, fz, (float) (x - fx), ry, (float) (z - fz), ry);
    }
    float getSmeared(double _x, double _y, double _z) const { // SmearedPerlinNoise.get
        double x = wrapCoordinate(_x) + offsetX, y = wrapCoordinate(_y) + offsetY, z = wrapCoordinate(_z) + offsetZ;
        int fx = floorInt(x), fy = floorInt(y), fz = floorInt(z);
        double relativeY = y - fy;
        double limit = (_y >= 0.0 && _y < relativeY) ? _y : relativeY;
        double fudge = floorInt(limit / fudgeYScale + 1.0000000116860974E-7) * fudgeYScale;
        return sampleAndLerp(fx, fy, fz, (float) (x - fx), (float) (relativeY - fudge), (float) (z - fz), (float) relativeY);
    }
};

struct GLayer { GPerlin perlin; double frequency; float amplitude; };

struct GNoise { // NoiseStack of plain Perlin layers
    std::vector<GLayer> layers;
    float get(double x, double y, double z) const {
        float value = 0.0f;
        for (const GLayer &l : layers) value += l.amplitude * l.perlin.get(x * l.frequency, y * l.frequency, z * l.frequency);
        return value;
    }
};

struct GSmearedStack {
    std::vector<GLayer> layers;
    float get(double x, double y, double z) const {
        float value = 0.0f;
        for (const GLayer &l : layers) value += l.amplitude * l.perlin.getSmeared(x * l.frequency, y * l.frequency, z * l.frequency);
        return value;
    }
    void create(Xoroshiro *random, int firstOctave, double smearScaleY, double valueFactor) { // BlendedNoise.createFbm
        int octaves = -firstOctave + 1; double factor = 1.0;
        valueFactor /= std::pow(2.0, octaves) - 1.0;
        for (int i = octaves - 1; i >= 0; i--) {
            GLayer layer; layer.perlin.init(random); layer.perlin.fudgeYScale = smearScaleY * factor; layer.frequency = factor; layer.amplitude = (float) valueFactor;
            layers.push_back(layer);
            factor /= 2.0; valueFactor *= 2.0;
        }
    }
};

struct GBlended { // old_blended_noise
    GSmearedStack minLimit, maxLimit, mainNoise;
    double xzMultiplier, yMultiplier, xzMain, yMain;
    void create(Xoroshiro *random, const double p[5]) { // xz_scale, y_scale, xz_factor, y_factor, smear_scale_multiplier
        xzMultiplier = 684.412 * p[0]; yMultiplier = 684.412 * p[1];
        double limitSmear = yMultiplier * p[4], mainSmear = limitSmear / p[3];
        minLimit.create(random, -15, limitSmear, 0.9999847412109375);
        maxLimit.create(random, -15, limitSmear, 0.9999847412109375);
        mainNoise.create(random, -7, mainSmear, 12.75);
        xzMain = xzMultiplier / p[2]; yMain = yMultiplier / p[3];
    }
    float get(int x, int y, int z) const {
        float choice = mainNoise.get(x * xzMain, y * yMain, z * xzMain) + 0.5f;
        choice = choice < 0.0f ? 0.0f : std::min(choice, 1.0f);
        if (choice == 0.0f) return minLimit.get(x * xzMultiplier, y * yMultiplier, z * xzMultiplier);
        if (choice == 1.0f) return maxLimit.get(x * xzMultiplier, y * yMultiplier, z * xzMultiplier);
        float low = minLimit.get(x * xzMultiplier, y * yMultiplier, z * xzMultiplier);
        return low + choice * (maxLimit.get(x * xzMultiplier, y * yMultiplier, z * xzMultiplier) - low);
    }
};

// NormalNoise.create for a named noise of a world seed (as RandomState.getOrCreateNoise)
static inline GNoise makeGameNoise(uint64_t worldLo, uint64_t worldHi, const char *name) {
    const NoiseParams *params = NULL;
    for (const NoiseParams &p : NOISE_PARAMS) if (!strcmp(p.name, name)) params = &p;
    if (!params) { fprintf(stderr, "no noise %s\n", name); __builtin_trap(); }
    const NameHash &hash = nameHash(name);
    Xoroshiro random = {worldLo ^ hash.lo, worldHi ^ hash.hi};
    uint64_t forkLo[2], forkHi[2];
    for (int half = 0; half < 2; half++) { forkLo[half] = xNextLong(&random); forkHi[half] = xNextLong(&random); }
    int n = params->octaveCount;
    double amplitude = params->baseAmplitude * (std::pow(0.5, -(n - 1)) / (std::pow(0.5, -n) - 1.0)), frequency = std::pow(2.0, params->baseOctave);
    std::vector<double> amplitudes, frequencies; std::vector<int> octaves; double target = 0, variance = 0;
    for (int i = 0; i < n; i++) {
        if (params->modifiers[i] != 0.0) {
            double modified = amplitude * params->modifiers[i];
            amplitudes.push_back(modified); frequencies.push_back(frequency); octaves.push_back(params->baseOctave + i);
            target += std::fabs(modified); double deviation = 0.2702247831245211 * std::fabs(modified); variance += deviation * deviation;
        }
        frequency *= 2.0; amplitude *= 0.5;
    }
    double normalization = (target * 0.3333333333333333) / (std::sqrt(variance) * std::sqrt(2.0));
    GNoise noise;
    for (size_t i = 0; i < octaves.size(); i++) {
        char octaveName[32]; snprintf(octaveName, sizeof octaveName, "octave_%d", octaves[i]);
        const NameHash &octaveHash = nameHash(octaveName);
        for (int half = 0; half < 2; half++) {
            GLayer layer; Xoroshiro octaveRandom = {forkLo[half] ^ octaveHash.lo, forkHi[half] ^ octaveHash.hi};
            layer.perlin.init(&octaveRandom);
            layer.frequency = frequencies[i] * (half ? 1.0181268882175227 : 1.0);
            layer.amplitude = (float) (normalization * amplitudes[i]);
            noise.layers.push_back(layer);
        }
    }
    return noise;
}

struct World {
    uint64_t seed, lo, hi;          // the world seed and RandomState's positional factory
    GNoise noise[GEN_NOISES];
    GBlended blended;
    uint64_t aquiferLo, aquiferHi;  // the aquifer's positional factory

    explicit World(uint64_t worldSeed) : seed(worldSeed) {
        Xoroshiro random; xSetSeed(&random, worldSeed);
        lo = xNextLong(&random); hi = xNextLong(&random);
        // seedcaves: the large-biomes preset unless CAVE_NORMAL is set. Its noise router is the overworld's with the
        // continents and erosion functions on the _large noises (NoiseRouterData.overworld(large = true): CONTINENTS_LARGE,
        // EROSION_LARGE and the offset, factor, jaggedness, depth, sloped cheese built on them; ridges, jagged, every cave
        // and aquifer noise unchanged), so two names are all that differs for terrain.
        static const bool large = getenv("CAVE_NORMAL") == NULL;
        for (int i = 0; i < GEN_NOISES; i++) {
            const char *name = GEN_NOISE_NAMES[i];
            if (large && !strcmp(name, "minecraft:erosion")) name = "minecraft:erosion_large";
            if (large && !strcmp(name, "minecraft:continentalness")) name = "minecraft:continentalness_large";
            noise[i] = makeGameNoise(lo, hi, name);
        }
        const NameHash &terrain = nameHash("minecraft:terrain");
        Xoroshiro terrainRandom = {lo ^ terrain.lo, hi ^ terrain.hi};
        blended.create(&terrainRandom, GEN_BLENDED);
        const NameHash &aquifer = nameHash("minecraft:aquifer");
        Xoroshiro aquiferRandom = {lo ^ aquifer.lo, hi ^ aquifer.hi};
        aquiferLo = xNextLong(&aquiferRandom); aquiferHi = xNextLong(&aquiferRandom);
    }
};

struct Col { float memo[GEN_SLOTS_2D]; unsigned char has[GEN_SLOTS_2D]; void clear() { memset(has, 0, sizeof has); } };
struct P {
    const World *w; int x, y, z; Col *col;
    float memo[GEN_SLOTS_3D]; unsigned char has[GEN_SLOTS_3D]; float interp[GEN_INTERPOLATED];
    void at(int newY) { y = newY; memset(has, 0, sizeof has); }
};

static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : std::min(v, hi); }
static inline float squeeze(float v) { float c = clampf(v, -1.0f, 1.0f); return c / 2.0f - c * c * c / 24.0f; }
static inline float gradient(int coordinate, int from, int to, float fromValue, float toValue) { // GradientFunction.ClampedSampler
    float factor = (toValue - fromValue) / (to - from);
    int lo = std::min(from, to), hi = std::max(from, to);
    int relative = std::min(std::max(coordinate, lo), hi) - from;
    return fromValue + relative * factor;
}
template <class Value> static inline float splineSample(float input, int n, const float *loc, const float *der, Value value) { // CubicSpline.Multipoint.sample
    int start = 0;
    while (start < n && !(input < loc[start])) start++;
    start--;
    if (start < 0) { float v = value(0), d = der[0]; return d == 0.0f ? v : v + d * (input - loc[0]); }
    if (start == n - 1) { float v = value(n - 1), d = der[n - 1]; return d == 0.0f ? v : v + d * (input - loc[n - 1]); }
    float x1 = loc[start], x2 = loc[start + 1], t = (input - x1) / (x2 - x1);
    float d1 = der[start], d2 = der[start + 1], y1 = value(start), y2 = value(start + 1);
    float a = d1 * (x2 - x1) - (y2 - y1), b = -d2 * (x2 - x1) + (y2 - y1);
    return lerpf(t, y1, y2) + t * (1.0f - t) * lerpf(t, a, b);
}
