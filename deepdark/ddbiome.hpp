// Biome lookups for the deep-dark aside, on cubiomes (MC_26_3, normal overworld). Kept in its own translation unit
// (ddbiome.cpp) so cubiomes' global names (ocean, river, none, ...) stay out of the terrain code.
//
// A biome cell is 4 x 4 x 4 blocks: cell (qx, qy, qz) holds blocks 4q .. 4q + 3 on each axis and has the biome the
// game's multi-noise source gives at quart position (qx, qy, qz) (what the game stores in the chunk).
// ddVoronoi is the game's BiomeManager "zoom": the cell whose biome the game reports for one block (F3, mob spawning);
// it is the block's own cell or one of the cells touching it.
#pragma once
#include <cstdint>

static const int DD_DEEP_DARK = 183; // cubiomes' id of deep_dark

struct DDBiome; // a seed's biome noise
struct DDClimate { float t, h, c, e, w; double off; }; // what does not change along a column of cells

DDBiome *ddNew(uint64_t seed);
void ddFree(DDBiome *b);
void ddColumn(const DDBiome *b, int qx, int qz, DDClimate *out);
int ddBiomeAt(const DDClimate *cc, int qy);                      // cubiomes biome id of cell (column, qy)
// Ties. The game compares squared distances in units of 1/10000 and deep dark (depth 1.1) meets the surface biomes
// (depth 1.0) at depth 1.05: a cell whose depth is exactly 10500 units is a tie. cubiomes gives it the surface biome;
// the game gives it whichever of the two its thread found last (Climate.RTree.lastResult), so the top cell of the deep
// dark layer of about 1 column in 300 depends on the order of the game's lookups. ddSetDepthBump(n) adds n units to the
// depth in ddBiomeAt: +2 = "deep dark, or could be in the game" (ties and a rounding margin), -2 = "deep dark for sure".
void ddSetDepthBump(int units);                                  // process-wide; set it before any lookup
int ddBiomeDirect(const DDBiome *b, int qx, int qy, int qz);     // the same through cubiomes' own sampleBiomeNoise
void ddVoronoi(const DDBiome *b, int x, int y, int z, int *qx, int *qy, int *qz);
const char *ddName(int id);
