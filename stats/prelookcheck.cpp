// gputables.cu's prelook against the CPU: for seeds on stdin the cheap first number from the GPU and from analyticgate.hpp
// (octave -8 alone, the lowest 1280-block window of each stack among windows 512 blocks apart), and the GPU's rate.
// build (box): g++ -O3 -march=native -std=c++17 -I . -I cubiomes prelookcheck.cpp gputables.o cub_noise.o -L/usr/local/cuda/lib64 -lcudart -lpthread -o prelookcheck
#include "tableseed.hpp"
#include <chrono>
#include <cstdio>
#include <iostream>
#include <string>
int main(int argc, char **argv) {
    const int batch = argc > 1 ? atoi(argv[1]) : 4096; std::vector<uint64_t> seeds; std::string l;
    while (std::getline(std::cin, l)) if (!l.empty()) seeds.push_back((uint64_t) std::stoll(l));
    if (gputables_init(64, batch)) return 1;
    std::vector<TableSeed> in(seeds.size()); std::vector<float> out(seeds.size());
    for (size_t i = 0; i < seeds.size(); i++) tableSeed(makeNoise(seeds[i], "minecraft:cave_cheese"), in[i]);
    auto now = [] { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); };
    double t0 = now();
    for (size_t i = 0; i < seeds.size(); i += batch) if (gpuprelook_batch(&in[i], (int) std::min<size_t>(batch, seeds.size() - i), &out[i])) return 1;
    double gpu = now() - t0;
    AnalyticGate low; const int N = 512; double worst = 0; size_t compared = std::min<size_t>(seeds.size(), argc > 2 ? atoi(argv[2]) : 300);
    for (size_t k = 0; k < compared; k++) {
        NormalNoise cheese = makeNoise(seeds[k], "minecraft:cave_cheese");
        for (int half = 0; half < 2; half++) { std::fill(low.field[half].begin(), low.field[half].end(), 0.0f); low.addLayer(cheese.layers[half], 0.6666666666666666, 0.5, low.field[half]); }
        low.wideValue(); float m[2] = {1, 1};
        for (int half = 0; half < 2; half++) for (int i = 0; i < N; i += 4) for (int j = 0; j < N; j += 4) m[half] = std::min(m[half], low.wide[half][(size_t) i * N + j]);
        printf("%lld %.4f %.4f\n", (long long) seeds[k], m[0] + m[1], out[k]); worst = std::max(worst, (double) std::fabs(m[0] + m[1] - out[k]));
    }
    fprintf(stderr, "%zu seeds on the GPU in %.2f s: %.0f seeds a second (batches of %d); %zu compared with the CPU: largest difference %.5f\n", seeds.size(), gpu, seeds.size() / gpu, batch, compared, worst);
}
