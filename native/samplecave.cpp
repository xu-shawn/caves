// Stage 2.5 of the cave search: the game's real blocks on a thin lattice around a site, and the largest cavern they show.
// Lattice: every 16th block in x and z, every 8th in y, inside a SIZE x SIZE box. Those blocks are corners of the game's
// density cells, so their density needs no interpolation: the native generator's corner functions and its aquifer give the
// exact block (air, rock, water, lava) at each. Underground air lattice points that touch (6 neighbours) are one cavern;
// its volume estimate is points x 2048 blocks. Unlike the cheese formula this knows the terrain, the other cave types,
// the pillars and the water.
// Output per site: "seed x z | largest cluster (blocks), all underground air (blocks), x range, y range, z range of the
// cluster, the cluster point nearest its centre (x y z: where to start the exact measurement) | rest of the input line"
// build: g++ -O3 -march=native -std=c++17 -ffp-contract=off -I ../vendor/cubiomes samplecave.cpp ../vendor/cub_noise.o -lpthread -o samplecave
// usage: samplecave [THREADS=8] [SIZE=1536] < "seed x y z ..."
#include "gamegen.hpp"
#include <atomic>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
int main(int argc, char **argv) {
    const int threads = argc > 1 ? atoi(argv[1]) : 8, size = argc > 2 ? atoi(argv[2]) : 1536, n = size / 16, levels = GEN_H / 8;
    struct Site { uint64_t seed; long x, z; std::string rest; }; std::vector<Site> sites; std::string line;
    while (std::getline(std::cin, line)) {
        Site s; long long seed; double x, y, z; int used = 0;
        if (sscanf(line.c_str(), "%lld %lf %lf %lf%n", &seed, &x, &y, &z, &used) >= 4) { s.seed = (uint64_t) seed; s.x = (long) x; s.z = (long) z; s.rest = line.substr(used); sites.push_back(s); }
    }
    std::map<uint64_t, std::unique_ptr<World>> worlds; // one per seed, shared by the threads
    for (const Site &s : sites) if (!worlds.count(s.seed)) worlds[s.seed].reset(new World(s.seed));
    std::vector<std::string> out(sites.size()); std::atomic<size_t> next(0); std::vector<std::thread> pool;
    for (int t = 0; t < threads; t++) pool.emplace_back([&] {
        std::vector<uint8_t> open((size_t) n * n * levels); std::vector<int> stack, members;
        for (size_t k; (k = next++) < sites.size();) {
            const Site &s = sites[k]; const World &world = *worlds[s.seed]; Generator generator(world);
            int x0 = (int) (((s.x - size / 2) >> 4) << 4), z0 = (int) (((s.z - size / 2) >> 4) << 4);
            Generator::Aquifer aquifer(generator, x0, GEN_MIN_Y, z0, size, GEN_H, size, false);
            long total = 0;
            for (int r = 0; r < n; r++) for (int c = 0; c < n; c++) {
                Col col; col.clear(); P p; p.w = &world; p.x = x0 + 16 * c; p.z = z0 + 16 * r; p.col = &col;
                int top = std::min(levels - 1, (generator.surfaceLevel(p.x, p.z) + 24 - GEN_MIN_Y) / 8); // nothing but sky above the surface
                uint8_t *cells = open.data() + ((size_t) r * n + c) * levels; bool roof = false;
                for (int l = levels - 1; l >= 0; l--) {
                    cells[l] = 0;
                    if (l > top) continue;
                    p.at(GEN_MIN_Y + 8 * l);
                    p.interp[0] = corner_0(p); p.interp[1] = corner_1(p); p.interp[2] = corner_2(p); p.interp[3] = corner_3(p); p.interp[4] = corner_4(p);
                    uint8_t block = aquifer.substance(p.x, p.y, p.z, (double) final_block(p));
                    if (block != AIR) roof = true; else if (roof) { cells[l] = 1; total++; }
                }
            }
            long best = 0; int box[6] = {0, 0, 0, 0, 0, 0}, centre[3] = {0, 0, 0};
            for (size_t start = 0; start < open.size(); start++) {
                if (open[start] != 1) continue;
                int lo[3] = {1 << 30, 1 << 30, 1 << 30}, hi[3] = {-1, -1, -1};
                open[start] = 2; stack.push_back((int) start); members.clear();
                while (!stack.empty()) {
                    int i = stack.back(); stack.pop_back(); members.push_back(i);
                    int l = i % levels, c = (i / levels) % n, r = i / (levels * n); int q[3] = {c, l, r};
                    for (int a = 0; a < 3; a++) { lo[a] = std::min(lo[a], q[a]); hi[a] = std::max(hi[a], q[a]); }
                    auto visit = [&](int j) { if (open[j] == 1) { open[j] = 2; stack.push_back(j); } };
                    if (l > 0) visit(i - 1); if (l + 1 < levels) visit(i + 1);
                    if (c > 0) visit(i - levels); if (c + 1 < n) visit(i + levels);
                    if (r > 0) visit(i - levels * n); if (r + 1 < n) visit(i + levels * n);
                }
                if ((long) members.size() > best) {
                    best = (long) members.size();
                    for (int a = 0; a < 3; a++) { box[2 * a] = lo[a]; box[2 * a + 1] = hi[a]; }
                    double mean[3] = {0, 0, 0};
                    for (int i : members) { mean[0] += (i / levels) % n; mean[1] += i % levels; mean[2] += i / (levels * n); }
                    for (double &m : mean) m /= best;
                    double nearest = 1e300;
                    for (int i : members) {
                        double dc = (i / levels) % n - mean[0], dl = (i % levels - mean[1]) * 0.5, dr = i / (levels * n) - mean[2], d = dc * dc + dl * dl + dr * dr;
                        if (d < nearest) { nearest = d; centre[0] = (i / levels) % n; centre[1] = i % levels; centre[2] = i / (levels * n); }
                    }
                }
            }
            char buf[512];
            snprintf(buf, sizeof buf, "%lld %ld %ld | %ld %ld %d %d %d %d %d %d %d %d %d |", (long long) s.seed, s.x, s.z, best * 2048, total * 2048, x0 + 16 * box[0], x0 + 16 * box[1], GEN_MIN_Y + 8 * box[2],
                     GEN_MIN_Y + 8 * box[3], z0 + 16 * box[4], z0 + 16 * box[5], x0 + 16 * centre[0], GEN_MIN_Y + 8 * centre[1], z0 + 16 * centre[2]);
            out[k] = buf + s.rest;
        }
    });
    for (auto &t : pool) t.join();
    for (auto &s : out) puts(s.c_str());
}
