// icecaves/icebiome.hpp: where the ice caves biome of Minecraft 26.4-snapshot-3 is, natively.
//
// The snapshot's biome list is 26.3's with three changes (OverworldBiomeBuilder.addUndergroundBiomes, read from the
// snapshot's jar): dripstone caves and sulfur caves take humidity -1 .. 0.7 only, and ice caves are added:
//     temperature -1 .. -0.8, humidity -1 .. 0.7, continentalness -1.05 .. -0.19 (the ocean range), erosion any,
//     weirdness 0.5 .. 1.1, depth 0.2 .. 0.9.
// The game gives a 4 x 4 x 4 cell the biome whose box is nearest to the cell's six climate numbers. A cell is ice caves
// when that box is nearer than every other: the surface biomes (which fill the five other numbers, at depth 0 and 1),
// lush caves, dripstone caves, sulfur caves and the deep dark. The climate is cubiomes' (the game's values; icecheck.cpp
// compares the cells with the game's biome source); the noise is sampled with fastnoise.hpp.
// Kept in its own translation unit so cubiomes' global names stay out of the terrain code.
#pragma once
#include <cstdint>

struct IceBiome;                                   // a seed's climate noise
struct IceClimate { float t, h, c, e, w; double off; };   // of a column of cells: nothing in it changes with the height
IceBiome *iceNew(uint64_t seed, bool large);
void iceFree(IceBiome *b);
// the climate at quart position (qx, qz); exact: with the shift noise and every octave (the game's values), else without
// the shift and the finest octaves, for looking around; terrain: also the offset (needed for the depth of a cell)
void iceClimate(const IceBiome *b, int qx, int qz, IceClimate *out, bool exact, bool terrain);
// the temperature alone at a quart position, without shift noise or the finest octaves: 4 noise samples (x 10000)
int64_t iceTemperature(const IceBiome *b, int qx, int qz);
// how far the climate is from the ice caves box in the five numbers that do not depend on the height (units of 1/10000,
// squared and summed; 0 inside the box)
int64_t iceBoxDistance(const IceClimate *c);
// is the cell at quart height qy ice caves? ties: 0 = a tie with another biome is not ice (default), 1 = it is
bool iceAt(const IceClimate *c, int qy, int ties);
// the cells of the column that are ice caves: bit (qy + 16) of out[0..1] for qy -16 .. 63
void iceColumn(const IceClimate *c, uint64_t out[2], int ties);
