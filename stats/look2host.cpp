// gpulook2.cu's three steps run on the host, thread by thread, against the CPU's low8y (cpuprelook.hpp): the same
// number up to the 16-bit rounding of the CPU look. For checking the kernel's arithmetic and indexing without a GPU.
// build: g++ -O3 -march=native -std=c++17 -I . -I vendor/cubiomes stats/look2host.cpp vendor/cub_noise.o -o stats/look2host
#include "cpuprelook.hpp"
#include "../gpulook2.cu"
#include <memory>
int main(int argc, char **argv) {
    int n = argc > 1 ? atoi(argv[1]) : 2000; uint64_t x = 0x9e3779b97f4a7c15ULL; double worst = 0, sum2 = 0; std::unique_ptr<CoarseLook> look(new CoarseLook); std::unique_ptr<LookShared> s(new LookShared);
    for (int i = 0; i < n; i++) {
        x ^= x << 13; x ^= x >> 7; x ^= x << 17; const uint64_t seed = x >> 16;
        TableSeed tables; tableSeed(seed, tables); LookSeed ls; lookSeed(seed, ls);
        for (int t = 0; t < 256; t++) lookTables(ls, *s, t);
        for (int t = 0; t < 256; t++) s->mins[t] = lookColumn(ls, *s, t);
        const float gpu = lookTotal(ls, *s), cpu = look->low8y(tables), exact = look->low8(tables);
        worst = std::max(worst, (double) std::fabs(gpu - cpu)); sum2 += (double) (gpu - cpu) * (gpu - cpu);
        if (i < 3) printf("seed %llu: kernel steps on the host %.6f, CPU low8y %.6f (all three gradient components: %.6f)\n", (unsigned long long) seed, gpu, cpu, exact);
    }
    printf("%d seeds: largest difference from low8y %.2e, rms %.2e\n", n, worst, std::sqrt(sum2 / n));
}
