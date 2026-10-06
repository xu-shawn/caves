// gputables.cu against the CPU: for seeds on stdin the two coarse-grid numbers from the GPU and from analyticgate.hpp's
// fields (every second window), and the GPU's rate. Prints "seed cpu512 cpuWide gpu512 gpuWide".
// build (on a box with CUDA): nvcc -O3 -c gpuscan.cu -o gpuscan.o && nvcc -O3 -c gputables.cu -o gputables.o && g++ -O3 -march=native -std=c++17 -I . -I cubiomes stats/tablecheck.cpp gputables.o cub_noise.o -L/usr/local/cuda/lib64 -lcudart -lpthread -o tablecheck
#include "tableseed.hpp"
#include <chrono>
#include <cstdio>
#include <iostream>
#include <string>
int main(int argc, char **argv) {
    const int batch = argc > 1 ? atoi(argv[1]) : 512; std::vector<uint64_t> seeds; std::string l;
    while (std::getline(std::cin, l)) if (!l.empty()) seeds.push_back((uint64_t) std::stoll(l));
    if (gputables_init(batch)) return 1;
    std::vector<TableSeed> in(seeds.size()); std::vector<float> out(2 * seeds.size());
    for (size_t i = 0; i < seeds.size(); i++) tableSeed(makeNoise(seeds[i], "minecraft:cave_cheese"), in[i]);
    auto now = [] { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); };
    double t0 = now();
    for (size_t i = 0; i < seeds.size(); i += batch) if (gputables_batch(&in[i], (int) std::min<size_t>(batch, seeds.size() - i), &out[2 * i])) return 1;
    double gpu = now() - t0;
    AnalyticGate gate; const int N = 512; std::vector<float> a(256 * 256), b(256 * 256); double worst = 0; size_t compared = std::min<size_t>(seeds.size(), argc > 2 ? atoi(argv[2]) : 300);
    for (size_t k = 0; k < compared; k++) {
        NormalNoise cheese = makeNoise(seeds[k], "minecraft:cave_cheese"); float lowest; gate.value(cheese, lowest); gate.wideValue();
        for (int i = 0; i < 256; i++) for (int j = 0; j < 256; j++) { a[i * 256 + j] = gate.field[0][(size_t) (2 * i) * N + 2 * j]; b[i * 256 + j] = gate.field[1][(size_t) (2 * i) * N + 2 * j]; }
        float cg = gate.quantile(a, b, 11544, lowest);
        for (int i = 0; i < 256; i++) for (int j = 0; j < 256; j++) { a[i * 256 + j] = gate.wide[0][(size_t) (2 * i) * N + 2 * j]; b[i * 256 + j] = gate.wide[1][(size_t) (2 * i) * N + 2 * j]; }
        float cw = gate.quantile(a, b, 62, lowest);
        printf("%lld %.4f %.4f %.4f %.4f\n", (long long) seeds[k], cg, cw, out[2 * k], out[2 * k + 1]);
        worst = std::max(worst, (double) std::max(std::fabs(cg - out[2 * k]), std::fabs(cw - out[2 * k + 1])));
    }
    fprintf(stderr, "%zu seeds on the GPU in %.2f s: %.0f seeds a second (batches of %d); %zu compared with the CPU: largest difference %.5f\n", seeds.size(), gpu, seeds.size() / gpu, batch, compared, worst);
}
