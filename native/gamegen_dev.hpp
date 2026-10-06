// The game's noise types for the GPU: gamegen_types.hpp with fixed-size layer lists and device functions, for the
// generated density functions (density_dev.hpp, made from density_gen.hpp by gpudensity's build line). The arithmetic is
// the same, operation for operation: build with nvcc --fmad=false (the game never fuses a multiply and an add), and the
// corner values come out bit for bit as on the CPU (gpucheck density).
#pragma once
#include <cstdint>
#include <cmath>
#ifdef __CUDACC__
#define GEN_FN __host__ __device__
#else
#define GEN_FN
#endif
GEN_FN static inline int floorInt(double v) { int i = (int) v; return v < i ? i - 1 : i; }
GEN_FN static inline int floorIntF(float v) { int i = (int) v; return v < i ? i - 1 : i; }
GEN_FN static inline float lerpf(float a, float b, float c) { return b + a * (c - b); }
GEN_FN static inline float smoothstepf(float x) { return x * x * x * (x * (x * 6.0f - 15.0f) + 10.0f); }
GEN_FN static inline double wrapCoordinate(double x) {
    const double half = 16777216.0 - 1.862645149230957e-09; // nextafter(1.6777216E7, 0.0): 2^24 - 2^-29
    return x >= -half && x < half ? x : x - floor(x / 3.3554432E7 + 0.5) * 3.3554432E7;
}
struct GPerlin {
    uint8_t perm[256];
    double offsetX, offsetY, offsetZ;
    double fudgeYScale; // only the smeared kind uses it
    GEN_FN int permute(int x) const { return perm[x & 0xFF]; }
    GEN_FN static float gradDot(int hash, float x, float y, float z) {
        // the 16 gradients of the game, as (x, y, z) components
        const int h = hash & 15;
        const int gx = (int) ((0x46552222u >> (2 * h)) & 3) - 1, gy = (int) ((0x2222550au >> (2 * h)) & 3) - 1, gz = (int) ((0x190a0a55u >> (2 * h)) & 3) - 1;
        return gx * x + gy * y + gz * z;
    }
    GEN_FN float sampleAndLerp(int x, int y, int z, float rx, float ry, float rz, float originalY) const {
        int x0 = permute(x), x1 = permute(x + 1);
        int xy00 = permute(x0 + y), xy01 = permute(x0 + y + 1), xy10 = permute(x1 + y), xy11 = permute(x1 + y + 1);
        float d000 = gradDot(permute(xy00 + z), rx, ry, rz), d100 = gradDot(permute(xy10 + z), rx - 1.0f, ry, rz);
        float d010 = gradDot(permute(xy01 + z), rx, ry - 1.0f, rz), d110 = gradDot(permute(xy11 + z), rx - 1.0f, ry - 1.0f, rz);
        float d001 = gradDot(permute(xy00 + z + 1), rx, ry, rz - 1.0f), d101 = gradDot(permute(xy10 + z + 1), rx - 1.0f, ry, rz - 1.0f);
        float d011 = gradDot(permute(xy01 + z + 1), rx, ry - 1.0f, rz - 1.0f), d111 = gradDot(permute(xy11 + z + 1), rx - 1.0f, ry - 1.0f, rz - 1.0f);
        float ax = smoothstepf(rx), ay = smoothstepf(originalY), az = smoothstepf(rz);
        return lerpf(az, lerpf(ay, lerpf(ax, d000, d100), lerpf(ax, d010, d110)), lerpf(ay, lerpf(ax, d001, d101), lerpf(ax, d011, d111)));
    }
    GEN_FN float get(double _x, double _y, double _z) const {
        double x = wrapCoordinate(_x) + offsetX, y = wrapCoordinate(_y) + offsetY, z = wrapCoordinate(_z) + offsetZ;
        int fx = floorInt(x), fy = floorInt(y), fz = floorInt(z);
        float ry = (float) (y - fy);
        return sampleAndLerp(fx, fy, fz, (float) (x - fx), ry, (float) (z - fz), ry);
    }
    GEN_FN float getSmeared(double _x, double _y, double _z) const {
        double x = wrapCoordinate(_x) + offsetX, y = wrapCoordinate(_y) + offsetY, z = wrapCoordinate(_z) + offsetZ;
        int fx = floorInt(x), fy = floorInt(y), fz = floorInt(z);
        double relativeY = y - fy;
        double limit = (_y >= 0.0 && _y < relativeY) ? _y : relativeY;
        double fudge = floorInt(limit / fudgeYScale + 1.0000000116860974E-7) * fudgeYScale;
        return sampleAndLerp(fx, fy, fz, (float) (x - fx), (float) (relativeY - fudge), (float) (z - fz), (float) relativeY);
    }
};
struct GLayer { GPerlin perlin; double frequency; float amplitude; int pad; };
static const int GEN_MAX_LAYERS = 32;
struct GNoise {
    GLayer layers[GEN_MAX_LAYERS]; int count, pad;
    GEN_FN float get(double x, double y, double z) const {
        float value = 0.0f;
        for (int i = 0; i < count; i++) { const GLayer &l = layers[i]; value += l.amplitude * l.perlin.get(x * l.frequency, y * l.frequency, z * l.frequency); }
        return value;
    }
};
struct GSmearedStack {
    GLayer layers[16]; int count, pad;
    GEN_FN float get(double x, double y, double z) const {
        float value = 0.0f;
        for (int i = 0; i < count; i++) { const GLayer &l = layers[i]; value += l.amplitude * l.perlin.getSmeared(x * l.frequency, y * l.frequency, z * l.frequency); }
        return value;
    }
};
struct GBlended {
    GSmearedStack minLimit, maxLimit, mainNoise;
    double xzMultiplier, yMultiplier, xzMain, yMain;
    GEN_FN float get(int x, int y, int z) const {
        float choice = mainNoise.get(x * xzMain, y * yMain, z * xzMain) + 0.5f;
        choice = choice < 0.0f ? 0.0f : (choice < 1.0f ? choice : 1.0f);
        if (choice == 0.0f) return minLimit.get(x * xzMultiplier, y * yMultiplier, z * xzMultiplier);
        if (choice == 1.0f) return maxLimit.get(x * xzMultiplier, y * yMultiplier, z * xzMultiplier);
        float low = minLimit.get(x * xzMultiplier, y * yMultiplier, z * xzMultiplier);
        return low + choice * (maxLimit.get(x * xzMultiplier, y * yMultiplier, z * xzMultiplier) - low);
    }
};
#ifndef GEN_NOISES
#define GEN_NOISES 29
#endif
struct World { GNoise noise[GEN_NOISES]; GBlended blended; };
// Lattice positions made ahead of the samples (gpudensity.cu): a noise sampled at a fixed scale of the block position
// (a "site") has the same x and z cell and place inside it all down a column, and the same y ones for every column of a
// level. XZ: per column and layer of every site; YL: per level and layer (original: SmearedPerlinNoise's unsmeared y).
// Sites: each site's noise and scales and where its layers start in those lists; after the sites come the three
// stacks of the blended noise (main, lower limit, upper limit).
struct XZ { float rx, rz; uint8_t fx, fz, pad[2]; };
struct YL { float ry, original; int32_t fy; };
static const int GEN_MAX_SITES = 48;
struct Sites { int32_t count, total; int32_t noise[GEN_MAX_SITES], first[GEN_MAX_SITES + 4]; double a[GEN_MAX_SITES], b[GEN_MAX_SITES], c[GEN_MAX_SITES]; };
#ifndef GEN_SLOTS_2D
#define GEN_SLOTS_2D 19
#define GEN_SLOTS_3D 18
#define GEN_INTERPOLATED 5
#endif
struct Col { float memo[GEN_SLOTS_2D]; unsigned char has[GEN_SLOTS_2D]; GEN_FN void clear() { for (int i = 0; i < GEN_SLOTS_2D; i++) has[i] = 0; } };
struct P {
    const World *w; int x, y, z; Col *col; const Sites *sites; const XZ *xz; const YL *yl;
    float memo[GEN_SLOTS_3D]; unsigned char has[GEN_SLOTS_3D]; float interp[GEN_INTERPOLATED];
    GEN_FN void at(int newY) { y = newY; for (int i = 0; i < GEN_SLOTS_3D; i++) has[i] = 0; }
};
GEN_FN static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (hi < v ? hi : v); }
GEN_FN static inline float squeeze(float v) { float c = clampf(v, -1.0f, 1.0f); return c / 2.0f - c * c * c / 24.0f; }
GEN_FN static inline float gradient(int coordinate, int from, int to, float fromValue, float toValue) {
    float factor = (toValue - fromValue) / (to - from);
    int lo = from < to ? from : to, hi = from < to ? to : from;
    int relative = (coordinate < lo ? lo : (coordinate > hi ? hi : coordinate)) - from;
    return fromValue + relative * factor;
}
template <class Value> GEN_FN static inline float splineSample(float input, int n, const float *loc, const float *der, Value value) {
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
// a site's noise from the prepared lattice positions: GNoise::get without its double-precision part
GEN_FN static inline float siteGet(P &p, int k) {
    const GNoise &n = p.w->noise[p.sites->noise[k]]; const XZ *xz = p.xz + p.sites->first[k]; const YL *yl = p.yl + p.sites->first[k]; float value = 0.0f;
    for (int i = 0; i < n.count; i++) { const GLayer &l = n.layers[i]; value += l.amplitude * l.perlin.sampleAndLerp(xz[i].fx, yl[i].fy, xz[i].fz, xz[i].rx, yl[i].ry, xz[i].rz, yl[i].ry); }
    return value;
}
GEN_FN static inline float smearedGet(const GSmearedStack &s, const XZ *xz, const YL *yl) {
    float value = 0.0f;
    for (int i = 0; i < s.count; i++) { const GLayer &l = s.layers[i]; value += l.amplitude * l.perlin.sampleAndLerp(xz[i].fx, yl[i].fy, xz[i].fz, xz[i].rx, yl[i].ry, xz[i].rz, yl[i].original); }
    return value;
}
GEN_FN static inline float blendedGet(P &p) { // GBlended::get
    const GBlended &b = p.w->blended; const int32_t *first = p.sites->first + p.sites->count;
    float choice = smearedGet(b.mainNoise, p.xz + first[0], p.yl + first[0]) + 0.5f;
    choice = choice < 0.0f ? 0.0f : (choice < 1.0f ? choice : 1.0f);
    if (choice == 0.0f) return smearedGet(b.minLimit, p.xz + first[1], p.yl + first[1]);
    if (choice == 1.0f) return smearedGet(b.maxLimit, p.xz + first[2], p.yl + first[2]);
    float low = smearedGet(b.minLimit, p.xz + first[1], p.yl + first[1]);
    return low + choice * (smearedGet(b.maxLimit, p.xz + first[2], p.yl + first[2]) - low);
}

