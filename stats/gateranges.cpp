// Tail probe: the two gate statistics (512-block windows at pair 184,700; 1280-block windows at pair 1,000) for the
// height range the header is built with (-DGATE_Y_LO=.. -DGATE_Y_HI=..). Prints "seed gate512 wide".
// build: g++ -O3 -march=native -std=c++17 -DGATE_Y_LO=-56 -DGATE_Y_HI=136 -I . -I vendor/cubiomes stats/gateranges.cpp vendor/cub_noise.o -lpthread -o gateranges
#include "analyticgate.hpp"
#include <atomic>
#include <cstdio>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
int main(int argc, char **argv) {
    int threads = argc > 1 ? atoi(argv[1]) : 8;
    std::vector<uint64_t> seeds; std::string l; while (std::getline(std::cin, l)) if (!l.empty()) seeds.push_back((uint64_t) std::stoll(l));
    std::atomic<size_t> next(0); std::mutex lock; std::vector<std::thread> pool;
    for (int t = 0; t < threads; t++) pool.emplace_back([&] {
        AnalyticGate gate;
        for (size_t k; (k = next++) < seeds.size();) {
            NormalNoise cheese = makeNoise(seeds[k], "minecraft:cave_cheese"); float lowest, value = gate.value(cheese, lowest), wide = gate.wideValue();
            std::lock_guard<std::mutex> guard(lock); printf("%lld %.4f %.4f\n", (long long) seeds[k], value, wide);
        }
    });
    for (auto &t : pool) t.join();
}
