// The GPU's corner values (gpudensity.cu) against the CPU generator's (gamegen.hpp), bit for bit, and their speed.
// build: g++ -O3 -march=native -std=c++17 -ffp-contract=off -I ../cubiomes densitycheck.cpp gpudensity.o ../cub_noise.o -L/usr/local/cuda/lib64 -lcudart -lpthread -o densitycheck
// usage: densitycheck SEED X Z [CPU_COLUMNS=4000] [GPU_COLUMNS=200000]     (columns on a square grid of 4 blocks around X, Z)
#include "gamegen.hpp"
#include "densitypack.hpp"
#include "gpudensity.h"
#include <chrono>
int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "usage: densitycheck SEED X Z [CPU_COLUMNS] [GPU_COLUMNS]\n"); return 1; }
    const uint64_t seed = strtoull(argv[1], 0, 10); const int x0 = atoi(argv[2]) & ~3, z0 = atoi(argv[3]) & ~3, cpuColumns = argc > 4 ? atoi(argv[4]) : 4000, gpuColumns = argc > 5 ? atoi(argv[5]) : 200000;
    auto now = [] { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); };
    if (sizeof(dev::World) != gpudensity_world_size()) { printf("layout differs: host %zu bytes, device %zu\n", sizeof(dev::World), gpudensity_world_size()); return 1; }
    World world(seed); std::unique_ptr<dev::World> packed(new dev::World); if (!packWorld(world, *packed)) return 1;
    if (gpudensity_init(gpuColumns, 4) || gpudensity_world(2, packed.get(), sizeof(dev::World))) return 1;
    const int side = (int) std::ceil(std::sqrt((double) gpuColumns)); std::vector<int> xz; for (int i = 0; i < gpuColumns; i++) { xz.push_back(x0 + 4 * (i % side - side / 2)); xz.push_back(z0 + 4 * (i / side - side / 2)); }
    std::vector<int> slot(gpuColumns, 2); const float *gpu = NULL;
    for (int n = 1; n < gpuColumns; n *= 8) { double t0 = now(); if (!gpudensity_run(xz.data(), slot.data(), n)) return 1; printf("%d columns on the GPU: %.3f s\n", n, now() - t0); fflush(stdout); }
    double a = now(); if (!(gpu = gpudensity_run(xz.data(), slot.data(), gpuColumns))) return 1; double b = now();
    AirMaker maker(world); long different = 0, values = 0; double worst = 0, c = now();
    for (int i = 0; i < cpuColumns; i++) {
        const int k = (int) ((long) i * gpuColumns / cpuColumns); const float *cpu = maker.cornerColumn(xz[2 * k], xz[2 * k + 1]), *g = gpu + (size_t) k * DENSITY_VALUES;
        for (int j = 0; j < DENSITY_VALUES; j++) { values++; if (memcmp(cpu + j, g + j, 4)) { different++; worst = std::max(worst, (double) std::fabs(cpu[j] - g[j])); } }
    }
    double d = now();
    printf("seed %llu around %d, %d: GPU %d columns in %.3f s (%.2f us a column); CPU %d columns in %.2f s (%.0f us a column); of %ld values compared %ld differ in their bits (largest difference %.3g)\n",
           (unsigned long long) seed, x0, z0, gpuColumns, b - a, (b - a) / gpuColumns * 1e6, cpuColumns, d - c, (d - c) / cpuColumns * 1e6, values, different, worst);
}
