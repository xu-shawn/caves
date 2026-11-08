# The largest ice caves cavern (Minecraft 26.4-snapshot-3)

Ice caves arrived in 26.4-snapshot-3 (6 October 2026). This is a seed search for the largest cavern in them, in two
categories:

- **ice caves throughout**: every block of the cavern is in an ice caves cell;
- **ice caves volume**: the cavern's blocks in ice caves cells, whatever the rest is.

Results are in `RESULTS.md` (written by `results.py` through `docs/build.py`); the write-up is `docs/ICECAVES.pdf`. The method is the cave search's (`../docs/PROGRESSION.md`):
a gate that says from the seed alone what a seed is worth, a scanner that lists a world's promising places from the
noise's periodicity without visiting the world, and exact measurement of the few that are left.

## What the snapshot does (read from its jar, `mc/`)

`OverworldBiomeBuilder.addUndergroundBiomes` adds, next to the other cave biomes (depth 0.2 to 0.9):

| | temperature | humidity | continentalness | erosion | weirdness |
|---|---|---|---|---|---|
| ice caves | -1 .. -0.8 | -1 .. 0.7 | -1.05 .. -0.19 (the ocean range) | any | 0.5 .. 1.1 |

In the same change dripstone caves and sulfur caves now take humidity -1 .. 0.7 only. The terrain is 26.3's: the
density functions, the noise router and the aquifer settings are identical, so the cave project's exact generator
still holds (caverns measured on the snapshot's own terrain come out the same, see Checks).

## The measure

A cavern as in the cave search: underground air within 3 blocks of a connected core of 7×7×7 air cubes, noise terrain
and aquifers only. A block is in ice caves when its 4×4×4 biome cell is.

## What an ice cavern is, and what limits one

The game gives a cell the biome whose box is *nearest* in the six climate numbers. Underground the surface biomes are
far away in depth, so ice caves reach past their box by up to min(depth, 1 - depth): a **lens** under a cold ocean,
widest near y -16 (weirdness down to about 0 still counts there), and narrow at both ends:

- at the roof, where the depth falls to 0.2 (y 10 to 35 under a sea floor at 35 to 48);
- at the floor: the lowest air (y -53, over the lava) is at depth 0.81 under an ocean, 0.19 from the box's end, so only
  weirdness over 0.31 is still ice caves there. Under land the depth passes 0.9 and the bottom cells are not ice caves
  at all.

Of 413 caverns that left the ice caves in an exact test, 72% left through the floor (y -56 to -40) and 28% through
the roof; the number at fault was the weirdness for 72%, the continentalness for 16%. Air is dry only well under the
sea floor (the aquifers' threshold falls from 0.4 at 64 blocks below the floor to -0.8 at the floor), so an ice cavern
is a slab between the lava and about y 0 to 20, some 22 blocks thick on average, and its size is its footprint:
**a cavern that is ice caves throughout is a cheese cave closed off inside one weirdness lobe.**

## The search

| stage | what it decides | cost |
|---|---|---|
| phase gate (`icephase gate`, `icegate` on a card) | are the wide layers of temperature and weirdness half way between their lattice planes at y 0, and the two lowest cheese layers where they should be at the caverns' height? (RNG steps only; stops at the first layer outside its window) | 30 ns a seed on a laptop thread, 0.06 ns on an RTX 4060 |
| scanner, pairs (`icepairs`) | 32 million cave-noise alignments on 6-step windows; only those with a place whose mean weirdness over the window ("box weirdness", from tables) is 0.40+ go on | 0.4 s |
| scanner, climate | which of the 1.7 million places with that weirdness are under ice caves climate? | 1.0 s |
| scanner, first look | coarse clusters (64.8-block tables, five heights) under an ice caves mask, for every such place (about 22,000) | 1.6 s |
| scanner, fine look | clusters on 32.4-block tables with the ice caves column by column, for the best 1,300 by a fitted index + 200 by ice points | 1.9 s |
| formula (`iceformula`) | the game's densities on a 32-block lattice around the 48 candidates most likely to be pure: is the cluster ice caves throughout, and how much of its rim is not? | 1.0 s |
| exact (`icemeasure`) | the cavern block by block, for pure clusters of 450+ samples; strict mode stops the moment the core leaves the ice caves | 2.9 s |

(Seconds a seed on one thread of a rented i9-14900KF with all 32 threads busy: 9 to 10 thread-seconds a seed with the
tables' 0.9 s. The laptop takes 12.)

### The gate: phases

A Perlin layer is weakest on its lattice planes and strongest half way between them, and the climate noises are only
ever sampled at y = 0. So where a layer's planes fall in y, one random number of the seed, decides how far that layer
swings anywhere in the world. Measured on 4,000 random seeds (`icephase stats`) and on the caverns found:

| layer (both stacks) | what it sets | effect on the log rate, per unit of -cos(2π phase) |
|---|---|---|
| temperature, octave -10 | the share of the world at -0.8 or colder: 0.55% to 2.3% (R² 0.96 from these two numbers) | 0.29 (cold share); 0.32 ± 0.14 (caverns of 2M+) |
| weirdness, octave -7 (512-block cells) | how large the lobes over 0.5 get | 0.37 (lobes 256 wide); about 1.0 (512 wide); 0.63 ± 0.20 (caverns of 2M+) |
| weirdness, octave -6 | the same, one size down | 0.37 (lobes 256 wide) |
| temperature, octave -8 | breaks cold areas up | -0.25 ± 0.13 (caverns of 2M+) |
| continentalness, humidity, cave_layer, aquifer floodedness | | nothing measurable |
| cave_cheese, octaves -8 and -7 | the cave noise at the ice caves' heights (96% of a seed's pair-mean threshold; best at 0.48 and 0.455 at y -16) | the cave gate's seeds are already there |

Every climate optimum is mid-cell. For the cave-gate seeds (a fixed list) the score in use is 0.32 (T10) - 0.12 (T8) +
0.63 (W7) + 0.37 (W6), each the sum over the two stacks of -cos(2π phase); fitted again on 2,129 scanned seeds its
coefficient is 0.96 ± 0.13 for caverns of 2M+.

**Fresh seeds: layers of windows.** `icephase gate FIRST COUNT THREADS SPEC` walks seeds and keeps those whose
layers all sit inside their windows (a layer = noise, octave, centre, half-width). The cost is the first test times
the product of the windows, so how the product is shared out is what matters:

- half-widths proportional to 1/√weight lose least for a given share of seeds kept (`gatefit.py` fits the weights and
  centres on what fresh seeds yielded and writes the spec);
- the cheese layers' weights grow with the cavern: octave -8 is worth 0.4 per stack for clusters of 270+ samples, 0.7
  for 350+, 0.9 to 1.0 for 430+ and for caverns of 2.5M+; its best phase at y -16 is 0.53 to 0.56 (strongest near the
  cavern floor), octave -7's 0.52 to 0.57;
- cave_entrance octave -7 (mid-cell at y -16) is worth 0.28 ± 0.07 on the largest clusters (3,150 seeds); the pillar
  noise on its plane 0.16 ± 0.10 (not settled; the one arm that gated on it did best);
- beyond the phases nothing about a seed is left to gate on: given its cave openness and its places under ice, the
  count of caverns of 2M+ is Poisson (dispersion 1.01), and neither the first look's best score nor the cave noise's
  lowest window mean is worth an early exit inside the scanner.

| seeds (same pipeline) | gate, thread-s per seed kept | 2M+ per seed | 2.5M+ | 3M+ |
|---|---|---|---|---|
| cave-gate seeds, best climate phases (2,400) | 0 (a fixed list) | 0.114 | 0.029 | 0.009 |
| fresh, climate phases only (1,236) | 0.2 | 0.126 | 0.012 | 0 found |
| fresh, climate tight and cheese loose (304) | 7 | 0.260 | 0.053 | 0.013 |
| fresh, ten windows by weight, cheese centred 0.48 / 0.455 (720) | 0.7 | 0.257 | 0.042 | 0.007 |
| fresh, twelve windows with the pillar layer, cheese centred 0.57 / 0.60 (890) | 1.7 | 0.315 | 0.067 | 0.020 |

**Tied phases.** Octaves of one noise stack are seeded by XOR-ing constants into one generator state, and the
generator's step is XOR-linear, so some octaves' y offsets move together (200,000 seeds): octave -5 sits half a cell
from -6; -8 mirrors -10; -6 follows -8 within 0.19; -9, -8 and -6 are tied in a checkerboard. -8/-7 and -7/-6 are
independent, as are the stacks. A gate must not multiply window shares over a tied pair (one with temperature -8
on its plane and -10 at mid-cell passed a tenth of the computed share), and weights fitted for tied layers are
collinear.

**How many spots per seed.** With a million pairs a seed, fewer was not better: the best 100,000 held 16% of the
large places of the best million, and two million 125%. Evaluating every place under ice at 16 million pairs
(`runs/oracle2`, `indexfit.py`) then showed that the cave-noise rank says nothing about a place's yield anywhere in
that range, while the yield rises sixfold from a box weirdness of 0.25 to 0.50: the cave noise only has to be good,
and what decides is the weirdness lobe over it. So the scanner now goes 32 times deeper in pairs and keeps those
with weirdness enough (next section).

### The scanner

The cave noise of a whole world is two small tables (stack A repeats every 1011 steps of 64.8 blocks, stack B every
993), and the pairs of low windows are listed from sorted lists; each pair is at 2 or 3 places per axis in the world.
What the ice caves version changes:

- **Pairs** are picked on 6-step windows over y -40 to 24 (the size of an ice cavern; 12 and 16 steps dated from a
  million pairs a seed: see the table under "How it got here").
- **The whole weirdness of a place from tables.** The weirdness has three octaves. Two repeat with the cave noise's
  period (65,536 blocks for stack A), so their mean over a window is a property of the pair. The widest (cells of
  512 blocks) repeats every two periods, and a pair's places are 331 periods apart, an odd number: the places fall
  into 2 x 2 classes, each with one exact mean weirdness over the middle 12 steps of the window ("box weirdness").
  `WBOX_MIN` drops, inside the loop that picks the pairs and from memory read in order, every pair with no class at
  that value: about a nanosecond a pair. Places under it never get a climate sample. The first look takes its
  weirdness from the same tables.
- **Climate at a million places a second.** The temperature's wide octave is nearly a plane across a window: its
  value and slope in the middle (two noise samples) bound it at all 16 quarter centres, and five windows in six go
  there. The rest ask only the quarters that can be cold, and a cold quarter gets the weirdness before anything else.
- **A first look at every place under ice caves** on the coarse tables: clusters of open, dry points with an ice caves
  mask from the temperature and continentalness at 16 points and the weirdness on 8×8, by depth. The best 1,000 of
  26,000 places by this hold 63 to 77% of the places whose fine cluster is large and ice caves throughout; the pair's
  open points, the cave search's order, held 12%.
- **The fine look** samples at y -53 (the lowest air) and up, applies the terrain's bottom slide, takes a point as ice
  caves only if the cells at both ends of the blocks it stands for are, and counts a cluster's **edge**: sideways
  neighbours that are rock or water outside the ice caves, where the cavern between the points can leave them.

### Formula and exact test

`iceformula` gives a cluster's samples and edge. Of its clusters of 230+ samples, in the game: ice caves throughout
for 81% with no edge, 55% with an edge under 2% of their size, 50% up to 4%, 30% beyond. A cavern has about 6,100
blocks per sample of its cluster.

Which candidates get the formula: the 48 best by size with a small charge for points outside the ice caves
(ln(score) - 0.04 x percent outside). With the formula run on every listed candidate of 651 seeds
(`runs/b4_np200`), 48 runs a seed by it hold 95% of the clusters the exact test wants (pure 500+, nearly pure 600+)
and all of the largest; 32 runs 90% and 99%. An index fitted earlier for small pure clusters (3.1 ln(size) - 0.54 x
percent outside: for clusters of 350+ samples its best tenth held 330 of 674 against 69 by size) held only 72% and
82% of the present targets: a ranking is as good as the target it was fitted for (`FORMULA_BY=index` keeps it).

`seed.sh` by default looks for the largest only: the strict exact test goes to pure clusters of 500+ samples (those
of 350 to 450 were three quarters of the tests and held none of 16 caverns of 3.5M+; those of 450 to 500 another 60%
with nothing over 4.1M), the full measure to ice clusters of 1,000+. Counts are complete from about 4M; with
`PURE_MIN=450` from 3.5M, with 350 from 2.5M.

**Nearly pure clusters get the strict test too.** The formula's rule for "ice caves" is the cautious one, and the
larger a cluster the likelier that a few of its samples are flagged: the cavern of 5,132,158 blocks has 792 samples,
11 of them flagged, so it is not a pure cluster, and it was only ever measured because 781 ice samples passed the
ice-volume threshold of the time (780). The new settings did not find it in its own seed (`toprecall.sh`: the whole
pipeline on the seeds of the 32 caverns of 3.6M+ known before; 25 found). So the strict test also goes to the
largest ice clusters of 600+ samples with at most 2% outside (tag N in `results.txt`; from 700 samples up to 5%, and
from 450 when the scanner's own cluster has 750+ points, since the formula's window is the smaller one). On 651 seeds
476 clusters of 600+ with up to 2% outside gave 24 caverns that are ice caves throughout, 10 of them of 3.5M+ that
the test of pure clusters does not reach; in production the first version found 3 of the 4 caverns of 5M+ and 11 of
the 21 of 4.5M+ of 4,660 seeds for a sixth of the test time, and the caverns of 4M+ a seed rose from 0.016 to 0.028.

## How it got here (same seeds, same laptop)

| build | thread-s a seed | fully ice 1.4M+ found in the 96 test seeds | 2M+ |
|---|---|---|---|
| D: cave scanner with a climate test per place | 23 | 10 | 2 |
| E: faster climate test, first look, new fine look | 13 | 7 | 4 |
| E2: near-pure clusters let through, wider limits | 17 | 36 | 8 |

Then the measuring policy for the largest only (12 thread-s a seed, 12 of 13 of the 2M+ kept), and the phase gate:

| seeds | fully ice 2M+ per thread-hour | 2.5M+ | tail: an e-fold per |
|---|---|---|---|
| cave-gate seeds in the cave gate's order | 9.3 | 1.3 | 0.30M |
| the same seeds, best phase score first | 25.4 | 8.5 | 0.56M |

Then the weirdness tables, deeper pairs, smaller windows and the ranking for the formula, the whole pipeline on
the same seeds each time (`pairtest.sh`; caverns that are ice caves throughout, exact test from 350 samples):

| setting | thread-s a seed | 2M+ | 2.5M+ | 3M+ | 3.5M+ |
|---|---|---|---|---|---|
| laptop, 200 seeds: 1M pairs, half by their own weirdness, windows of 12 and 16 | 8.1 | 22 | 6 | 3 | 2 |
| 16M pairs, box weirdness 0.35+ | 9.6 | 98 | 54 | 16 | 2 |
| windows of 12 only, formula candidates by the index | 11.5 | 146 | 76 | 25 | 5 |
| windows of 10 | 12.9 | 184 | 102 | 37 | 12 |
| windows of 8 | 14.6 | 281 | 172 | 67 | 16 |
| rented box, 651 seeds: windows of 10 | 10.0 | 653 | 348 | 106 | 27 |
| windows of 8 | 12.2 | 885 | 551 | 203 | 46 |
| windows of 6 | 13.4 | 1,166 | 807 | 292 | 66 |
| windows of 4 | 14.0 | 1,223 | 921 | 307 | 55 |
| windows of 6, 32M pairs at 0.40+ (in use) | 13.6 | 1,184 | 835 | 316 | 64 |

The caverns of 4.25M+ in those 651 seeds are nearly the same five or six for every window size from 10 to 6: the
multiplier is in the 2.5M to 4M range, and above 3.5M the tail falls by an e-fold per 0.29M +- 0.03M (0.42 +- 0.08
before). Not better: a weirdness box of 8 steps (same caverns, scanner twice as dear), 2,000 fine looks with 64
formula candidates, 64M pairs at 0.45+, an early exit on what the first look shows (a seed's count of large caverns
is Poisson, variance over mean 0.9 to 1.0).

### What bounds a record (`tailwhy.py`, `icelobe`, `icepatch`)

A cavern that is ice caves throughout is a cave sealed inside one patch of ice caves, and the larger the cave the
rarer that is: of the scanner's clusters 47% of those of 450 to 500 points stay inside the ice caves in the exact
test, 15% at 600 to 650, 6% at 700 to 800, 3.7% at 800 to 900 (`tailwhy.py`, 69,000 exact tests). The failures of
700+ points leave by the weirdness (68%; continentalness 18%, depth 9%, temperature 6%), 55% of them at the floor.
`icelobe` measures the patch around a place (columns that are ice caves from y -56 to 16): 0.37 to 0.85 km2 (10 to
90%), 80% of its edge set by the weirdness, where a cavern of 5 to 6M is 800 to 1,000 blocks long; `icepatch` counts
such patches per world (a square of 512 blocks inside one: 22 among 3.2 million slab columns of 2,400 gated worlds).
The patch's area does not predict a passing cavern's size (7 to 14 blocks of cavern a column) but it does the chance
to pass (1 to 2% of clusters of 700+ points under 0.5 km2, 5 to 7% from 0.65). No seed-level layer separates the
caverns of 4.3M+ from the rest, and the weirdness octave of 256-block cells is right at mid-cell for large patches
too. What moved the tail was the test of nearly pure clusters: caverns of 5M+ per 1,000 seeds 0.23 -> 0.85, the
e-fold above 4M 0.23M -> 0.30M.

**Largest only.** `PLACES=700 PLACES_ICE=100 SP_MIN=560 NP=24 PURE_MIN=530 NEAR_MIN=650` takes two thirds of the
time a seed and still finds 46 of the 49 caverns of 4.7M+ known when it was set (`LIST=top_new ./toprecall.sh NAME
JOBS VAR=...`); the runs `box4` and `prod6` use it, so their counts are complete from 4.7M only.

What was tried and left: a world sweep of one seed (the first attempt: 1.95M in seed 1 after 106 squares); pairs
picked by weirdness as well as cave noise (the top tenth of places by window weirdness holds only 43% of the good
ones); the scanner's edge as a filter (the largest caverns come from clusters with an edge); the number of quarter
centres in the box as a filter.

## Running it

```
../vendor/build.sh                            # once: cubiomes objects (every command here runs from icecaves/)
# build lines are at the top of each source: icebiome.cpp (first), icepairs.cpp, iceformula.cpp, icemeasure.cpp, icephase.cpp
./seed.sh SEED runs/NAME                      # one seed, one thread
./runset.sh NAME 12                           # the seeds in runs/NAME/seeds.txt, 12 at a time, with a yield summary
./longrun.sh NAME SEEDS_FILE 12 600           # a long list in chunks; resumes; `touch runs/NAME/STOP` ends it
./gaterun.sh NAME SPEC_FILE 2 10              # fresh seeds: the gate on 2 threads feeding 10 workers, in chunks; resumes
./icephase gate FIRST COUNT THREADS "$(cat spec_p1.txt)"   # the gate alone: seeds whose layers are in their windows
./pairtest.sh NAME SEEDS_FILE JOBS VAR=value  # the whole pipeline on a seed list under a setting: caverns and time
./recall.sh NAME JOBS PAIRS PLACES VAR=value  # the scanner alone: known caverns still listed (SEEDS=file + distinct.py: clusters listed)
python3 indexfit.py runs/oracle2/*.all        # the fine look's ranking judged by clusters, from full-evaluation dumps
./verify_top.sh 8                             # the game's biome source for the largest caverns not yet checked
fleet/setup.sh b1; fleet/guard.sh CAP b1 RUN  # a rented box: upload and build; the spending guard (see fleet/*.sh)
python3 gatefit.py --target clusters430 RUNS  # weights, centres and windows from what fresh seeds yielded
./scanvar.sh NAME SEEDS JOBS PAIRS PLACES VAR=value   # the scanner alone under a setting, to compare settings
./icephase phases < seeds                     # every layer's phase of given seeds
python3 tail.py NAME [NAME ...] --top 10      # leaders, counts per thread-hour, tail fit
```

Seeds scanned so far (179,000): the cave gate's 330,000 (its output, not in the repository) in the order of the
climate phase score (`runs/long2/all_seeds_v2.txt`, the first 2,400); fresh seeds from the gate on the laptop
(`runs/fresh_*`, `runs/prod1` to `prod6`; the new scanner from `prod4`); and 130,108 fresh seeds on a rented box with
the gate on its card (`runs/box1` to `box4`, 7 October 2026, $1.45). `runs/*/notes.txt` and `settings.txt` say where a
run's policy changed; `runs/pt*`, `b_*`, `b2_*`, `b3_near`, `b4_np200`, `tr_*`, `tn_*` are the tests on fixed seeds.

## Checks

- The biome rule: 5,999 of 6,000 cells near the ice caves box equal the game (`icecheck`, `java/BiomeProbe`).
- The phases: `icephase stats` compares its offsets with cubiomes' for every seed it looks at.
- Caverns: `measure_snapshot.sh` measures a cavern on the terrain the snapshot's own code makes; `verify_ice.py SEED X
  Y Z` asks the snapshot's biome source for every cell of a cavern. The leaders in `RESULTS.md` say which checks they
  have passed.

## Files

| | |
|---|---|
| `icephase.cpp` | layer phases from the seed; `stats` (world statistics per seed), `scan` (the gate), `phases` |
| `icepairs.cpp` | the scanner (the cave project's `seedcaves/ringpairs.cpp`, changed as above) |
| `icebiome.cpp`, `.hpp` | which cells are ice caves (nearest-box rule with the snapshot's boxes, cubiomes' climate) |
| `iceformula.cpp` | clusters on a lattice of the game's densities, with the edge |
| `icemeasure.cpp` | the cave project's exact measurer plus the ice caves count and the strict mode |
| `seed.sh`, `runset.sh`, `longrun.sh`, `gaterun.sh`, `yield.py`, `tail.py`, `results.py` | drivers and summaries |
| `icegate.cu` | the gate on a CUDA card: same spec and output as `icephase gate`, every hit checked by the CPU code |
| `gatefit.py`, `spec_*.txt`, `scanvar.sh` | the gate's fit and specs; scanner-setting comparisons |
| `pairtest.sh`, `recall.sh`, `distinct.py`, `placefit.py`, `indexfit.py`, `index_coarse.txt` | settings compared on the same seeds; the fine look's ranking |
| `fleet/` | a rented box: `setup.sh` (upload, build), `start.sh` and `rate.sh` (on the box), `guard.sh` (spending cap, copies, download before destroy), `selfstop.sh` (the box's own backup) |
| `verify_top.sh`, `checked.txt` | the leaders' checks against the game |
| `tailwhy.py`, `icelobe.cpp`, `icepatch.cpp`, `toprecall.sh`, `abgate.py` | what bounds the largest: fates and exits of the big candidates, the ice patch around a place, large patches per world, recall of known large caverns under a setting, two gates side by side |
| `iceseeds.cpp` | the cold and ocean share of a world by sampling (what the phases predict) |
| `measure_snapshot.sh`, `verify_ice.py`, `java/` | the checks against the snapshot |
| `icesites.cpp`, `square.sh`, `scan.sh`, `look.py` | the first attempt: a sweep of one world |
| `post/` | map of the 7.26M cavern that has 48 rim blocks outside the ice caves |

## Not in the repository

- **The snapshot.** `measure_snapshot.sh`, `verify_ice.py`, `verify_top.sh` and the tools in `java/` need the
  26.4-snapshot-3 server jar unpacked under `icecaves/mc/` (`mc/bundle/` for the jar and its libraries as listed in
  `java/classpath.txt`) and a Java 25 runtime (`JAVA25=/path/to/java`). Compile the tools once, from this folder:
  `javac -cp "$(cat java/classpath.txt)" -d java/out java/*.java`.
- **The box helper.** `fleet/` calls `box.sh` and reads `boxes.txt` from a checkout of desert-search next to this
  repository (`../../desert-search/deploy/` from here), as the cave search's `fleet/` does.
- **Run data.** `runs/` (every run's seeds, per-seed logs and measured caverns), the first attempt's `work/` and the
  downloads from the rented box are left out. `results.py`, `tail.py`, `docs/build.py` and the analysis scripts read
  them, and the runs named in this README (`runs/oracle2`, `runs/b4_np200`, ...) are among them.
