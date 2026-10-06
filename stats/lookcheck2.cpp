// cpuprelook2.hpp (the z filter made once per layer) against cpuprelook.hpp: the four looks on seeds from stdin (or N
// pseudo-random ones), the largest differences, how often a cut decides differently, and the time of each.
// build: g++ -O3 -march=native -std=c++17 -I . -I vendor/cubiomes stats/lookcheck2.cpp vendor/cub_noise.o -o stats/lookcheck2
// usage: lookcheck2 [N=2000]      or      lookcheck2 0 < seeds
#include "cpuprelook.hpp"
#include "cpuprelook2.hpp"
#include <chrono>
#include <iostream>
#include <memory>
#include <string>
static double now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
int main(int argc, char **argv) {
    int n = argc > 1 ? atoi(argv[1]) : 2000; std::vector<uint64_t> seeds; std::string l;
    if (n == 0) { while (std::getline(std::cin, l)) if (!l.empty()) seeds.push_back((uint64_t) std::stoll(l)); }
    else { uint64_t s = 0x9e3779b97f4a7c15ULL; for (int i = 0; i < n; i++) { s ^= s << 13; s ^= s >> 7; s ^= s << 17; seeds.push_back(s >> 16); } }
    std::unique_ptr<CoarseLook> a(new CoarseLook); std::unique_ptr<CoarseLook2> b(new CoarseLook2); std::vector<TableSeed> tables(seeds.size());
    for (size_t i = 0; i < seeds.size(); i++) tableSeed(seeds[i], tables[i]);
    const char *names[4] = {"low8y (16 bit)", "lowBothY (16 bit)", "low8 (float)", "lowBoth (float)"}; const float cuts[4] = {-0.1748f, -0.202f, 0, -0.2076f};
    std::vector<float> va[4], vb[4]; double ta[4] = {0}, tb[4] = {0};
    for (int pass = 0; pass < 2; pass++) for (int which = 0; which < 4; which += 2) { // (the second of each pair needs the first of the same seed)
        std::vector<float> &x = pass ? vb[which] : va[which], &y = pass ? vb[which + 1] : va[which + 1]; x.resize(seeds.size()); y.resize(seeds.size());
        double t0 = now();
        for (size_t i = 0; i < seeds.size(); i++) x[i] = pass ? (which ? b->low8(tables[i]) : b->low8y(tables[i])) : (which ? a->low8(tables[i]) : a->low8y(tables[i]));
        double t1 = now();
        for (size_t i = 0; i < seeds.size(); i++) { x[i] = pass ? (which ? b->low8(tables[i]) : b->low8y(tables[i])) : (which ? a->low8(tables[i]) : a->low8y(tables[i])); y[i] = pass ? (which ? b->lowBoth(tables[i]) : b->lowBothY(tables[i])) : (which ? a->lowBoth(tables[i]) : a->lowBothY(tables[i])); }
        double t2 = now();
        (pass ? tb : ta)[which] = (t1 - t0) / seeds.size(); (pass ? tb : ta)[which + 1] = (t2 - t1 - (t1 - t0)) / seeds.size();
    }
    for (int which = 0; which < 4; which++) {
        double worst = 0, sum2 = 0; size_t flips = 0, under = 0;
        for (size_t i = 0; i < seeds.size(); i++) { double d = vb[which][i] - va[which][i]; worst = std::max(worst, std::fabs(d)); sum2 += d * d; flips += (va[which][i] <= cuts[which]) != (vb[which][i] <= cuts[which]); under += va[which][i] <= cuts[which]; }
        printf("%-18s old %6.2f us, new %6.2f us (x%.1f); largest difference %.2e, rms %.2e; %zu of %zu under the cut %.4f, %zu decided differently\n", names[which], ta[which] * 1e6, tb[which] * 1e6, ta[which] / tb[which], worst, std::sqrt(sum2 / seeds.size()), under, seeds.size(), cuts[which], flips);
    }
    printf("example: seed %llu old %.6f %.6f %.6f %.6f new %.6f %.6f %.6f %.6f\n", (unsigned long long) seeds[0], va[0][0], va[1][0], va[2][0], va[3][0], vb[0][0], vb[1][0], vb[2][0], vb[3][0]);
}
