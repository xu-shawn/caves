// Cavern volumes in a terrain dump of CaveDump (bytes for z, x, y: 0 solid, 1 air, 2 water, 3 lava; y from -64 to 319).
//
// Underground air = air blocks with at least one non-air block somewhere above them (the open sky doesn't count).
// A cavern for the passage width W = 2 R + 1: take the underground air a W x W x W cube can reach without touching
// anything else (the cube's centres are the "core"), split the cores into connected pieces (6 neighbours), and give every
// piece the air its cube sweeps. So two rooms joined only by a passage narrower than W blocks are two caverns, and R = 0
// is plain connectivity. The volume of a cavern is its number of air blocks. Cores under 2,000 blocks are left out, and
// air that the cubes of two caverns both reach goes to the one with the bigger core.
//
// build: g++ -O3 -march=native -std=c++17 cavemetric.cpp -o cavemetric
// usage: cavemetric DUMP X0 Z0 SIZE_X SIZE_Z [R=2] [TOP=5] [PX PY PZ]     (with a point: also the cavern that block is in)
// output: one line per cavern, largest first: volume, core blocks, bounding box (world coordinates), "at" the block in the
// middle of the cavern's core (the centre of an empty 7 x 7 x 7 cube: a place to teleport to), "edge" if it touches
// the side of the dump (then it is cut off and the volume is a lower bound)
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>
int main(int argc, char **argv) {
    if (argc < 6) { fprintf(stderr, "usage: cavemetric DUMP X0 Z0 SIZE_X SIZE_Z [R=2] [TOP=5]\n"); return 1; }
    const int x0 = atoi(argv[2]), z0 = atoi(argv[3]), sx = atoi(argv[4]), sz = atoi(argv[5]), R = argc > 6 ? atoi(argv[6]) : 2, top = argc > 7 ? atoi(argv[7]) : 5, fullY = 384;
    std::vector<uint8_t> grid((size_t) sx * sz * fullY);
    FILE *f = fopen(argv[1], "rb");
    if (!f || fread(grid.data(), 1, grid.size(), f) != grid.size()) { fprintf(stderr, "can't read %zu bytes from %s\n", grid.size(), argv[1]); return 1; }
    fclose(f);
    // underground air; only the heights that have any are kept from here on
    size_t airCount = 0, undergroundCount = 0; int highest = 0;
    for (size_t column = 0; column < (size_t) sx * sz; column++) {
        bool roof = false; uint8_t *cells = grid.data() + column * fullY;
        for (int y = fullY - 1; y >= 0; y--) {
            if (cells[y] != 1) { roof = true; cells[y] = 0; }
            else { airCount++; cells[y] = roof; if (roof) { undergroundCount++; if (y > highest) highest = y; } }
        }
    }
    const int sy = std::min(fullY, highest + 2);
    const size_t n = (size_t) sx * sz * sy;
    auto at = [&](int x, int y, int z) { return ((size_t) z * sx + x) * sy + y; };
    std::vector<uint8_t> air(n);
    for (size_t column = 0; column < (size_t) sx * sz; column++) std::copy(grid.begin() + column * fullY, grid.begin() + column * fullY + sy, air.begin() + column * sy);
    std::vector<uint8_t>().swap(grid);
    // core: erosion by the cube, one axis at a time (outside the dump counts as rock)
    std::vector<uint8_t> core = air, pass(n);
    const long strides[3] = {(long) sy, 1, (long) sx * sy}; const int sizes[3] = {sx, sy, sz};
    for (int axis = 0; axis < 3 && R > 0; axis++) {
        std::fill(pass.begin(), pass.end(), 0);
        for (int z = 0; z < sz; z++) for (int x = 0; x < sx; x++) for (int y = 0; y < sy; y++) {
            int c = axis == 0 ? x : axis == 1 ? y : z; size_t i = at(x, y, z);
            if (c - R < 0 || c + R >= sizes[axis]) continue;
            uint8_t all = 1;
            for (int d = -R; d <= R && all; d++) all &= core[i + d * strides[axis]];
            pass[i] = all;
        }
        core.swap(pass);
    }
    // connected pieces of the core
    // 16-bit labels keep the big dumps in memory: only cores of at least MIN_CORE blocks get one (at most 65,534 of them)
    const size_t MIN_CORE = 2000;
    std::vector<uint16_t> label(n, 0); std::vector<size_t> stack, members; int labels = 0; std::vector<size_t> coreSize(1, 0);
    std::vector<double> sumX(1, 0), sumY(1, 0), sumZ(1, 0); // for the centre of each core
    for (size_t s = 0; s < n; s++) {
        if (core[s] != 1) continue;
        core[s] = 2; stack.push_back(s); members.clear(); double mx = 0, my = 0, mz = 0; // 2 = visited
        while (!stack.empty()) {
            size_t i = stack.back(); stack.pop_back(); members.push_back(i);
            int y = (int) (i % sy), x = (int) ((i / sy) % sx), z = (int) (i / ((size_t) sx * sy));
            mx += x; my += y; mz += z;
            auto visit = [&](size_t j) { if (core[j] == 1) { core[j] = 2; stack.push_back(j); } };
            if (x > 0) visit(i - sy); if (x + 1 < sx) visit(i + sy);
            if (y > 0) visit(i - 1); if (y + 1 < sy) visit(i + 1);
            if (z > 0) visit(i - (size_t) sx * sy); if (z + 1 < sz) visit(i + (size_t) sx * sy);
        }
        if (members.size() < MIN_CORE || labels >= 65534) continue;
        labels++; coreSize.push_back(members.size()); sumX.push_back(mx); sumY.push_back(my); sumZ.push_back(mz);
        for (size_t i : members) label[i] = (uint16_t) labels;
    }
    std::vector<size_t>().swap(members);
    // Renumber the cores by size: where the cubes of two caverns reach the same air, the cavern with the bigger core
    // gets it (the bigger label wins below), so the largest cavern's volume is exactly the air its cube sweeps.
    {
        std::vector<int> bySize(labels); for (int l = 0; l < labels; l++) bySize[l] = l + 1;
        std::sort(bySize.begin(), bySize.end(), [&](int a, int b) { return coreSize[a] < coreSize[b]; });
        std::vector<uint16_t> renumber(labels + 1, 0); std::vector<size_t> size2(labels + 1, 0); std::vector<double> x2(labels + 1), y2(labels + 1), z2(labels + 1);
        for (int rank = 0; rank < labels; rank++) { int old = bySize[rank]; renumber[old] = (uint16_t) (rank + 1); size2[rank + 1] = coreSize[old]; x2[rank + 1] = sumX[old]; y2[rank + 1] = sumY[old]; z2[rank + 1] = sumZ[old]; }
        for (size_t i = 0; i < n; i++) if (label[i]) label[i] = renumber[label[i]];
        coreSize.swap(size2); sumX.swap(x2); sumY.swap(y2); sumZ.swap(z2);
    }
    // the air each piece's cube sweeps: spread the labels by R along each axis (where two pieces reach, the larger label wins)
    // a place to stand: the core block nearest to the core's centre
    std::vector<double> nearest(labels + 1, 1e300); std::vector<size_t> standing(labels + 1, 0);
    for (int z = 0; z < sz; z++) for (int x = 0; x < sx; x++) for (int y = 0; y < sy; y++) {
        size_t i = at(x, y, z); int l = label[i];
        if (!l) continue;
        double dx = x - sumX[l] / coreSize[l], dy = y - sumY[l] / coreSize[l], dz = z - sumZ[l] / coreSize[l], d = dx * dx + dy * dy + dz * dz;
        if (d < nearest[l]) { nearest[l] = d; standing[l] = i; }
    }
    std::vector<uint8_t>().swap(core); std::vector<uint8_t>().swap(pass); // not needed any more, the big dumps need the room
    std::vector<uint16_t> spread; spread.swap(label);
    std::vector<uint16_t> passLabel(n);
    for (int axis = 0; axis < 3 && R > 0; axis++) {
        for (int z = 0; z < sz; z++) for (int x = 0; x < sx; x++) for (int y = 0; y < sy; y++) {
            int c = axis == 0 ? x : axis == 1 ? y : z; size_t i = at(x, y, z); uint16_t best = 0;
            for (int d = std::max(-R, -c); d <= R && c + d < sizes[axis]; d++) best = std::max(best, spread[i + d * strides[axis]]);
            passLabel[i] = best;
        }
        spread.swap(passLabel);
    }
    struct Cavern { size_t volume = 0, core = 0; int lo[3] = {1 << 30, 1 << 30, 1 << 30}, hi[3] = {-(1 << 30), -(1 << 30), -(1 << 30)}; bool edge = false; };
    std::vector<Cavern> caverns(labels + 1);
    for (int z = 0; z < sz; z++) for (int x = 0; x < sx; x++) for (int y = 0; y < sy; y++) {
        size_t i = at(x, y, z);
        if (!air[i] || !spread[i]) continue;
        Cavern &c = caverns[spread[i]]; c.volume++;
        int p[3] = {x, y, z};
        for (int k = 0; k < 3; k++) { c.lo[k] = std::min(c.lo[k], p[k]); c.hi[k] = std::max(c.hi[k], p[k]); }
        if (x == 0 || z == 0 || x == sx - 1 || z == sz - 1) c.edge = true;
    }
    for (int l = 1; l <= labels; l++) caverns[l].core = coreSize[l];
    std::vector<int> order(labels); for (int l = 0; l < labels; l++) order[l] = l + 1;
    std::sort(order.begin(), order.end(), [&](int a, int b) { return caverns[a].volume > caverns[b].volume; });
    printf("%d x %d columns at (%d, %d): %zu air blocks, %zu underground; passage width %d: %d caverns\n", sx, sz, x0, z0, airCount, undergroundCount, 2 * R + 1, labels);
    if (argc > 10) {
        int px = atoi(argv[8]) - x0, py = atoi(argv[9]) + 64, pz = atoi(argv[10]) - z0;
        if (px < 0 || pz < 0 || px >= sx || pz >= sz || py < 0 || py >= sy) printf("point: outside the dump (or above every cave)\n");
        else {
            size_t i = at(px, py, pz); int l = spread[i];
            if (!air[i]) printf("point: not underground air\n");
            else if (!l) printf("point: underground air, but in a passage narrower than %d\n", 2 * R + 1);
            else {
                const Cavern &c = caverns[l]; int rank = 1;
                for (int other = 1; other <= labels; other++) rank += caverns[other].volume > c.volume;
                printf("point: cavern of %zu blocks (core %zu), x %d..%d y %d..%d z %d..%d%s, number %d by volume in this dump\n", c.volume, c.core, x0 + c.lo[0], x0 + c.hi[0], c.lo[1] - 64, c.hi[1] - 64,
                       z0 + c.lo[2], z0 + c.hi[2], c.edge ? " edge" : "", rank);
            }
        }
    }
    for (int k = 0; k < top && k < labels; k++) {
        const Cavern &c = caverns[order[k]];
        size_t i = standing[order[k]];
        printf("%zu %zu x %d..%d y %d..%d z %d..%d at %d %d %d%s\n", c.volume, c.core, x0 + c.lo[0], x0 + c.hi[0], c.lo[1] - 64, c.hi[1] - 64, z0 + c.lo[2], z0 + c.hi[2], x0 + (int) ((i / sy) % sx),
               (int) (i % sy) - 64, z0 + (int) (i / ((size_t) sx * sy)), c.edge ? " edge" : "");
    }
}
