| caves per box-hour | 30M+ | 34M+ | 38M+ | 40M+ | 42M+ | 44M+ | a factor 10 per |
|----------------------------------------------------|--------|--------|--------|--------|--------|--------|-------------|
| v2 00:55 fast exact measure | 1.2 | 0.08 | | | | | 3.4M |
| v3 02:05 terrain-first scanner | 11 | 1.1 | | | | | 3.4M |
| v4 02:41 phase gate | 87 | 5.7 | | | | | 3.4M |
| v6 04:21 early exit; exact by F and O | 231 | 27 | 4.5 | 0.8 | 0.8 | 0.8 | 4.7M |
| v8 06:06 gate on both window sizes | 373 | 58 | 8.6 | 2.9 |  |  | 4.8M |
| v10 07:04 scan and first look on the GPU | 1,021 | 210 | 24 | 2.4 |  |  | 5.4M |
| v11 07:28 phase windows from the GPU, leaner workers | 945 | 257 | 47 | 22 | 12 | 5.8 | 6.4M |
| v12 08:20 percolation term in the gate | 937 | 277 | 49 | 16 | 7.2 | 3.9 | 6.3M |
| v13 10:08 cheap look on the GPU, wider filters | 1,265 | 404 | 71 | 25 | 8.8 | 3.5 | 6.6M |
| v14 10:42 cheap looks on CPU threads (fit from 38M) |  |  | 144 | 67 | 26 | 7.9 | 5.1M |
