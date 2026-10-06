| build | hours | cost | 38M+ | 40M+ | 42M+ | 44M+ | 46M+ | largest to expect for $1 | for $10 |
|------------------------------|---|---|---|---|---|---|---|-----|---|
| box 1, percolation gate | 1.80 | $0.29 | 311 | 98 | 45 | 24 | 14 | 49.4M ± 0.4 | 54.1M |
| box 1, last build (CPU coarse looks) | 1.90 | $0.30 | 907 | 420 | 166 | 50 | 13 | 52.4M ± 0.2 | 57.1M |
| box 2, gate on the GPU | 0.22 | $0.07 | 580 | 244 | 61 | 31 | 15 | 51.2M ± 0.5 | 55.9M |
| box 2, workers on the GPU too | 0.71 | $0.21 | 1,542 | 661 | 249 | 86 | 29 | 53.3M ± 0.2 | 58.0M |
| box 2, last build | 1.49 | $0.44 | 1,421 | 581 | 225 | 59 | 32 | 53.0M ± 0.1 | 57.7M |
| box 2, whole run | 4.95 | $1.46 | 1,267 | 523 | 199 | 64 | 25 | 52.8M ± 0.1 | 57.5M |
| box 3, z kernel made once (all of it) | 2.94 | $0.87 | 1,924 | 783 | 292 | 104 | 43 | 53.6M ± 0.1 | 58.3M |
| box 3, its last 75 minutes | 1.29 | $0.38 | 1,983 | 823 | 329 | 121 | 45 | 53.7M ± 0.1 | 58.4M |

(Distinct caverns of at least that volume per dollar of rented box. Largest to expect: where the fitted rate times the budget is one cavern, with one slope for all builds, a factor 10 per 4.7M ± 0.1M from 1599 caverns of 40M+; the ± is from the build's own count of 40M+ caverns.)
