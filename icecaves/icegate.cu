// icecaves/icegate.cu: the phase gate (icephase gate) on a CUDA card.
// Same SPEC and the same output as `icephase gate`: "SEED phases..." for the seeds whose listed noise layers sit within
// their windows (see icephase.cpp). The card does the scan in integers (a phase is the low 45 bits of the 53 a
// nextDouble uses, as 256 x double mod 1); every index it reports is checked again by the host with icephase's own
// arithmetic before it is printed, so a line here is a line of the CPU gate (a seed exactly on a window's edge, one in
// 1e13, can go missing).
// usage: icegate FIRST COUNT SPEC      (env PER=4096 indices a thread, THREADS=1048576 threads a launch)
//   stderr: the rate, every 10 launches and at the end.
// build: nvcc -O3 -I .. -I ../vendor/cubiomes icegate.cu -o icegate
extern "C" {
#include "rng.h"
}
#include "name_hashes.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <cuda_runtime.h>

struct DevLayer { uint64_t lo, hi, at[2], centre, half; };            // (phases in units of 2^-45)
struct DevNoise { uint64_t lo, hi; int first, count; };
static const int MAX_NOISES = 16, MAX_LAYERS = 48; static const uint64_t ONE = 1ULL << 45, MASK = ONE - 1;
__constant__ DevNoise cNoise[MAX_NOISES]; __constant__ DevLayer cLayer[MAX_LAYERS]; __constant__ int cNoises;

__device__ __forceinline__ uint64_t rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }
__device__ __forceinline__ uint64_t nextLong(uint64_t &lo, uint64_t &hi) { const uint64_t l = lo; uint64_t h = hi; const uint64_t n = rotl(l + h, 17) + l; h ^= l; lo = rotl(l, 49) ^ h ^ (h << 21); hi = rotl(h, 28); return n; }
__device__ __forceinline__ uint64_t mixIndexDev(uint64_t i) { uint64_t z = i * 0x9E3779B97F4A7C15ULL; z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL; z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL; return z ^ (z >> 31); }

__global__ void scanKernel(uint64_t first, uint32_t per, uint64_t count, uint32_t *found, uint64_t *out, uint32_t room) {
    const uint64_t tid = (uint64_t) blockIdx.x * blockDim.x + threadIdx.x, from = tid * per;
    for (uint32_t k = 0; k < per; k++) {
        const uint64_t i = from + k; if (i >= count) return;
        const uint64_t seed = mixIndexDev(first + i);
        uint64_t l = seed ^ 0x6a09e667f3bcc909ULL, h = l + 0x9e3779b97f4a7c15ULL;                    // xSetSeed
        l = (l ^ (l >> 30)) * 0xbf58476d1ce4e5b9ULL; h = (h ^ (h >> 30)) * 0xbf58476d1ce4e5b9ULL;
        l = (l ^ (l >> 27)) * 0x94d049bb133111ebULL; h = (h ^ (h >> 27)) * 0x94d049bb133111ebULL;
        l ^= l >> 31; h ^= h >> 31;
        const uint64_t worldLo = nextLong(l, h), worldHi = nextLong(l, h);
        bool pass = true;
        for (int n = 0; n < cNoises && pass; n++) {
            uint64_t rl = worldLo ^ cNoise[n].lo, rh = worldHi ^ cNoise[n].hi;
            for (int half = 0; half < 2 && pass; half++) {
                const uint64_t forkLo = nextLong(rl, rh), forkHi = nextLong(rl, rh);
                for (int q = cNoise[n].first; q < cNoise[n].first + cNoise[n].count; q++) {
                    const DevLayer &g = cLayer[q]; uint64_t ol = forkLo ^ g.lo, oh = forkHi ^ g.hi;
                    nextLong(ol, oh); const uint64_t v = ((nextLong(ol, oh) >> 11) + g.at[half]) & MASK;
                    uint64_t d = v > g.centre ? v - g.centre : g.centre - v; if (d > ONE / 2) d = ONE - d;
                    if (d > g.half) { pass = false; break; }
                }
            }
        }
        if (pass) { const uint32_t slot = atomicAdd(found, 1u); if (slot < room) out[slot] = i; }
    }
}

struct GateLayer { NameHash octave; double at, centre, half; };
struct GateNoise { NameHash noise; std::vector<GateLayer> layers; };
static inline uint64_t mixIndex(uint64_t i) { uint64_t z = i * 0x9E3779B97F4A7C15ULL; z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL; z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL; return z ^ (z >> 31); }
// (icephase.cpp's test of one seed: the phases into p, in SPEC's order)
static bool passes(uint64_t seed, const std::vector<GateNoise> &noises, double *p, int *count) {
    Xoroshiro world; xSetSeed(&world, seed); const uint64_t worldLo = xNextLong(&world), worldHi = xNextLong(&world); int at = 0;
    for (size_t n = 0; n < noises.size(); n++) {
        Xoroshiro random = {worldLo ^ noises[n].noise.lo, worldHi ^ noises[n].noise.hi};
        for (int half = 0; half < 2; half++) {
            const uint64_t forkLo = xNextLong(&random), forkHi = xNextLong(&random);
            for (size_t k = 0; k < noises[n].layers.size(); k++) {
                const GateLayer &g = noises[n].layers[k]; Xoroshiro r = {forkLo ^ g.octave.lo, forkHi ^ g.octave.hi};
                xNextDouble(&r); double v = xNextDouble(&r) * 256.0 + g.at * (half ? 337.0 / 331.0 : 1.0); v -= std::floor(v);
                double d = std::fabs(v - g.centre); if (d > 0.5) d = 1 - d;
                if (d > g.half) return false;
                p[at + 2 * k + half] = v;
            }
        }
        at += 2 * (int) noises[n].layers.size();
    }
    *count = at; return true;
}

int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "usage: icegate FIRST COUNT SPEC\n"); return 1; }
    const uint64_t first = strtoull(argv[1], 0, 10), count = strtoull(argv[2], 0, 10);
    std::vector<GateNoise> noises; std::string text(argv[3]); size_t from = 0; std::string lastName; double kept = 1; std::vector<std::string> keep; keep.reserve(256);
    while (from < text.size()) {
        size_t to = text.find(';', from); if (to == std::string::npos) to = text.size();
        char name[64], octave[16]; double at, centre, half;
        if (sscanf(text.substr(from, to - from).c_str(), " %63[^,],%15[^,],%lf,%lf,%lf", name, octave, &at, &centre, &half) == 5) {
            if (noises.empty() || lastName != name) { keep.push_back(std::string("minecraft:") + name); noises.push_back({nameHash(keep.back().c_str()), {}}); lastName = name; }
            keep.push_back(std::string("octave_") + octave); noises.back().layers.push_back({nameHash(keep.back().c_str()), at, centre, half}); kept *= 4 * half * half;
        }
        from = to + 1;
    }
    DevNoise dn[MAX_NOISES]; DevLayer dl[MAX_LAYERS]; int layers = 0;
    if ((int) noises.size() > MAX_NOISES) { fprintf(stderr, "too many noises\n"); return 1; }
    auto fixed = [](double v) { v -= std::floor(v); return (uint64_t) std::llround(v * (double) ONE) & MASK; };
    for (size_t n = 0; n < noises.size(); n++) {
        dn[n] = {noises[n].noise.lo, noises[n].noise.hi, layers, (int) noises[n].layers.size()};
        for (const GateLayer &g : noises[n].layers) {
            if (layers >= MAX_LAYERS) { fprintf(stderr, "too many layers\n"); return 1; }
            // (a hair wider than the window: the host's check decides the edge)
            dl[layers++] = {g.octave.lo, g.octave.hi, {fixed(g.at), fixed(g.at * 337.0 / 331.0)}, fixed(g.centre), std::min(ONE / 2, (uint64_t) std::llround(g.half * (double) ONE) + 64)};
        }
    }
    const int noiseCount = (int) noises.size();
    cudaMemcpyToSymbol(cNoise, dn, sizeof dn); cudaMemcpyToSymbol(cLayer, dl, sizeof dl); cudaMemcpyToSymbol(cNoises, &noiseCount, sizeof noiseCount);
    const uint32_t per = getenv("PER") ? (uint32_t) atoi(getenv("PER")) : 4096, threads = getenv("THREADS") ? (uint32_t) atoi(getenv("THREADS")) : 1 << 20, room = 1 << 22;
    uint32_t *dFound; uint64_t *dOut; cudaMalloc(&dFound, sizeof(uint32_t)); cudaMalloc(&dOut, (size_t) room * sizeof(uint64_t));
    if (cudaGetLastError() != cudaSuccess) { fprintf(stderr, "no CUDA device or no memory\n"); return 1; }
    std::vector<uint64_t> hits(room); const uint64_t launch = (uint64_t) per * threads; uint64_t printed = 0, dropped = 0; int launches = 0;
    fprintf(stderr, "gate of %zu noises on the card: %.3g of the seeds pass\n", noises.size(), kept);
    const auto t0 = std::chrono::steady_clock::now();
    for (uint64_t done = 0; done < count; done += launch) {
        const uint64_t n = std::min(launch, count - done); uint32_t zero = 0, found = 0;
        cudaMemcpy(dFound, &zero, sizeof zero, cudaMemcpyHostToDevice);
        scanKernel<<<(threads + 255) / 256, 256>>>(first + done, per, n, dFound, dOut, room);
        cudaMemcpy(&found, dFound, sizeof found, cudaMemcpyDeviceToHost);
        if (cudaGetLastError() != cudaSuccess) { fprintf(stderr, "kernel failed\n"); return 1; }
        if (found > room) { fprintf(stderr, "more hits than room in a launch (%u): narrow the gate or lower PER\n", found); found = room; }
        cudaMemcpy(hits.data(), dOut, (size_t) found * sizeof(uint64_t), cudaMemcpyDeviceToHost);
        std::sort(hits.begin(), hits.begin() + found);
        for (uint32_t k = 0; k < found; k++) {
            const uint64_t seed = mixIndex(first + done + hits[k]); double p[2 * MAX_LAYERS]; int phases = 0;
            if (!passes(seed, noises, p, &phases)) { dropped++; continue; }
            printf("%lld", (long long) seed); for (int q = 0; q < phases; q++) printf(" %.3f", p[q]); printf("\n"); printed++;
        }
        fflush(stdout);
        if (++launches % 10 == 0) { const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); fprintf(stderr, "%.3e scanned, %.3e a second, %llu kept\n", (double) (done + n), (done + n) / s, (unsigned long long) printed); }
    }
    const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    fprintf(stderr, "%llu seeds scanned in %.1f s (%.3e a second), %llu kept (%llu of the card's were outside by the host's check)\n", (unsigned long long) count, s, count / s, (unsigned long long) printed, (unsigned long long) dropped);
    return 0;
}
