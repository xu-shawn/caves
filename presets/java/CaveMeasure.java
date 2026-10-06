import it.unimi.dsi.fastutil.longs.LongArrayFIFOQueue;
import it.unimi.dsi.fastutil.longs.LongArrayList;
import it.unimi.dsi.fastutil.longs.LongOpenHashSet;
import net.minecraft.core.registries.Registries;
import net.minecraft.server.Bootstrap;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.level.levelgen.Aquifer;
import net.minecraft.world.level.levelgen.NoiseChunk;
import net.minecraft.world.level.levelgen.RandomState;
import net.minecraft.world.level.levelgen.blending.Blender;
import net.minecraft.world.level.levelgen.densityfunction.DensitySampler;
import net.minecraft.world.level.levelgen.densityfunction.DensityVolume;
import net.minecraft.world.level.levelgen.densityfunction.ScopedDensityBuffer;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicInteger;

/**
 * The exact volume of a cavern, measured by a flood fill that asks the real game for terrain only where the cavern goes.
 *
 * The measure is cavemetric's with a passage width of 7: underground air = air blocks with a non-air block above them;
 * the core = the centres of 7 x 7 x 7 cubes of underground air; a cavern = a connected piece of the core (6 neighbours)
 * and all the underground air within 3 blocks (Chebyshev) of it. Chunks come from the same noise fill as CaveDump
 * (NoiseChunk and the aquifer, no structures, carvers or features), for y -64 .. 255 (the game has no terrain above 255).
 *
 * usage: CaveMeasure THREADS < lines "SEED X Y Z [anything]"
 * The fill starts at the block X Y Z if it is in a core, otherwise in the largest piece of core within 4 chunks of it.
 * prints "SEED X Z VOLUME CORE x0 x1 y0 y1 z0 z1 at TX TY TZ chunks N seconds S | rest" (TX TY TZ: a core block near the
 * middle of the cavern, a place to teleport to).
 */
public class CaveMeasure {
    static final int MIN_Y = -64, H = Integer.getInteger("cave.height", 320), R = 3, GROW = 2;   // (cave.height 384 for the amplified world type, whose terrain goes up to y 320)
    static final byte AIR = 1, CORE = 2, SEEN = 4;

    static int threads;
    static long seed;
    static final ThreadLocal<RandomState[]> RANDOM = ThreadLocal.withInitial(() -> new RandomState[1]);
    static final ThreadLocal<long[]> RANDOM_SEED = ThreadLocal.withInitial(() -> new long[] {0});
    static ConcurrentHashMap<Long, byte[]> chunks;
    static LongOpenHashSet withCore;

    static long key(int cx, int cz) { return ((long) cx << 32) ^ (cz & 0xffffffffL); }
    static int keyX(long key) { return (int) (key >> 32); }
    static int keyZ(long key) { return (int) key; }
    static long pack(int x, int y, int z) { return ((long) (x + 33554432) << 36) | ((long) (z + 33554432) << 10) | (y - MIN_Y); }
    static int packX(long p) { return (int) (p >>> 36) - 33554432; }
    static int packZ(long p) { return (int) ((p >>> 10) & 0x3ffffff) - 33554432; }
    static int packY(long p) { return (int) (p & 1023) + MIN_Y; }

    static RandomState randomState() {
        RandomState[] holder = RANDOM.get(); long[] holderSeed = RANDOM_SEED.get();
        if (holder[0] == null || holderSeed[0] != seed) {
            holder[0] = RandomState.create(CaveDump.registries.lookupOrThrow(Registries.NOISE), seed, CaveDump.noiseSettings);
            holderSeed[0] = seed;
        }
        return holder[0];
    }

    /** The underground air of a chunk: a byte per block, index (z * 16 + x) * H + (y - MIN_Y). */
    static byte[] generate(int cx, int cz) {
        byte[] cells = new byte[256 * H];
        int minX = cx << 4, minZ = cz << 4;
        DensityVolume volume = new DensityVolume(16, H, 16, minX, MIN_Y, minZ);
        try (NoiseChunk noiseChunk = new NoiseChunk(randomState(), null, CaveDump.noiseSettings, CaveDump.fluidPicker, Blender.empty(), volume)) {
            Aquifer aquifer = noiseChunk.aquifer();
            DensitySampler.Bound finalDensity = noiseChunk.cachingSamplers().get(CaveDump.noiseSettings.noiseRouter().finalDensity());
            try (ScopedDensityBuffer densities = finalDensity.sampleVolume(noiseChunk.volume())) {
                for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
                    boolean roof = false; int base = (z * 16 + x) * H;
                    for (int y = H - 1; y >= 0; y--) {
                        BlockState state = aquifer.computeSubstance(minX + x, MIN_Y + y, minZ + z, densities.get(volume.indexUnchecked(x, y, z)));
                        if (state != null && state.isAir()) { if (roof) cells[base + y] = AIR; }
                        else roof = true;
                    }
                }
            }
        }
        return cells;
    }

    static void parallel(List<Runnable> jobs) throws InterruptedException {
        AtomicInteger next = new AtomicInteger();
        Thread[] pool = new Thread[Math.min(threads, Math.max(1, jobs.size()))];
        for (int t = 0; t < pool.length; t++) {
            pool[t] = new Thread(() -> { for (int i; (i = next.getAndIncrement()) < jobs.size(); ) jobs.get(i).run(); });
            pool[t].start();
        }
        for (Thread thread : pool) thread.join();
    }

    /** One flag of the 3 x 3 chunks around a chunk, padded by R each way: index ((pz * P) + px) * PH + py with P = 16 + 2R, PH = H + 2R. */
    static final int P = 16 + 2 * R, PH = H + 2 * R;
    static byte[] padded(int cx, int cz, byte flag) {
        byte[] out = new byte[P * P * PH];
        for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) {
            byte[] cells = chunks.get(key(cx + dx, cz + dz));
            if (cells == null) continue;
            int x0 = Math.max(0, -R - 16 * dx), x1 = Math.min(15, 15 + R - 16 * dx), z0 = Math.max(0, -R - 16 * dz), z1 = Math.min(15, 15 + R - 16 * dz);
            for (int z = z0; z <= z1; z++) for (int x = x0; x <= x1; x++) {
                int from = (z * 16 + x) * H, to = ((z + 16 * dz + R) * P + (x + 16 * dx + R)) * PH + R;
                for (int y = 0; y < H; y++) out[to + y] = (byte) ((cells[from + y] & flag) != 0 ? 1 : 0);
            }
        }
        return out;
    }

    /** Along one axis of the padded array: 1 where all (erode) or any (dilate) of the 2R + 1 cells around are 1. */
    static void filter(byte[] a, byte[] out, int stride, int length, boolean erode) {
        int lines = a.length / length;
        // the lines along an axis: for stride 1 they are consecutive runs; otherwise walk the other two axes
        if (stride == 1) {
            for (int line = 0; line < lines; line++) run(a, out, line * length, 1, length, erode);
        } else if (stride == PH) {          // along x: lines are (pz, py)
            for (int pz = 0; pz < P; pz++) for (int py = 0; py < PH; py++) run(a, out, pz * P * PH + py, PH, P, erode);
        } else {                             // along z: lines are (px, py)
            for (int px = 0; px < P; px++) for (int py = 0; py < PH; py++) run(a, out, px * PH + py, P * PH, P, erode);
        }
    }

    static void run(byte[] a, byte[] out, int start, int stride, int length, boolean erode) {
        int sum = 0;
        for (int i = 0; i < Math.min(R, length); i++) sum += a[start + i * stride];
        for (int i = 0; i < length; i++) {
            if (i + R < length) sum += a[start + (i + R) * stride];
            if (i - R - 1 >= 0) sum -= a[start + (i - R - 1) * stride];
            out[start + i * stride] = (byte) (erode ? (sum == 2 * R + 1 ? 1 : 0) : (sum > 0 ? 1 : 0));
        }
    }

    static byte[] filtered(int cx, int cz, byte flag, boolean erode) {
        byte[] a = padded(cx, cz, flag), b = new byte[a.length];
        filter(a, b, 1, PH, erode);
        filter(b, a, PH, P, erode);
        filter(a, b, P * PH, P, erode);
        return b;
    }

    /** Makes sure the chunks in keys have their core: loads them and their neighbours, then erodes. */
    static void ensureCore(LongOpenHashSet keys) throws InterruptedException {
        LongOpenHashSet toLoad = new LongOpenHashSet();
        for (long k : keys) for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) {
            long n = key(keyX(k) + dx, keyZ(k) + dz);
            if (!chunks.containsKey(n)) toLoad.add(n);
        }
        List<Runnable> jobs = new ArrayList<>();
        for (long k : toLoad) jobs.add(() -> chunks.put(k, generate(keyX(k), keyZ(k))));
        parallel(jobs);
        jobs.clear();
        for (long k : keys) {
            if (withCore.contains(k)) continue;
            jobs.add(() -> {
                int cx = keyX(k), cz = keyZ(k);
                byte[] core = filtered(cx, cz, AIR, true), cells = chunks.get(k);
                synchronized (cells) {
                    for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
                        int from = ((z + R) * P + (x + R)) * PH + R, to = (z * 16 + x) * H;
                        for (int y = 0; y < H; y++) if (core[from + y] != 0) cells[to + y] |= CORE;
                    }
                }
            });
        }
        parallel(jobs);
        withCore.addAll(keys);
    }

    static byte get(int x, int y, int z) {
        if (y < MIN_Y || y >= MIN_Y + H) return 0;
        byte[] cells = chunks.get(key(x >> 4, z >> 4));
        return cells == null ? 0 : cells[((z & 15) * 16 + (x & 15)) * H + (y - MIN_Y)];
    }

    static void set(int x, int y, int z, byte flag) {
        chunks.get(key(x >> 4, z >> 4))[((z & 15) * 16 + (x & 15)) * H + (y - MIN_Y)] |= flag;
    }

    static String measure(int startX, int startY, int startZ) throws InterruptedException {
        long t0 = System.nanoTime();
        chunks = new ConcurrentHashMap<>();
        withCore = new LongOpenHashSet();
        int cx0 = startX >> 4, cz0 = startZ >> 4;
        LongOpenHashSet first = new LongOpenHashSet();
        for (int dz = -4; dz <= 4; dz++) for (int dx = -4; dx <= 4; dx++) first.add(key(cx0 + dx, cz0 + dz));
        ensureCore(first);

        // where to start: the given block if it is core, else the largest piece of core among the first chunks
        long start = -1;
        if ((get(startX, startY, startZ) & CORE) != 0) start = pack(startX, startY, startZ);
        else {
            long bestSize = 0; LongArrayList touched = new LongArrayList(); LongArrayFIFOQueue queue = new LongArrayFIFOQueue();
            for (long k : first) {
                int bx = keyX(k) << 4, bz = keyZ(k) << 4;
                for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) for (int y = 0; y < H; y++) {
                    if ((get(bx + x, MIN_Y + y, bz + z) & (CORE | SEEN)) != CORE) continue;
                    long size = 0, from = pack(bx + x, MIN_Y + y, bz + z);
                    set(bx + x, MIN_Y + y, bz + z, SEEN); queue.enqueue(from); touched.add(from);
                    while (!queue.isEmpty()) {
                        long p = queue.dequeueLong(); size++;
                        int px = packX(p), py = packY(p), pz = packZ(p);
                        for (int d = 0; d < 6; d++) {
                            int nx = px + (d == 0 ? 1 : d == 1 ? -1 : 0), ny = py + (d == 2 ? 1 : d == 3 ? -1 : 0), nz = pz + (d == 4 ? 1 : d == 5 ? -1 : 0);
                            if (!first.contains(key(nx >> 4, nz >> 4)) || (get(nx, ny, nz) & (CORE | SEEN)) != CORE) continue;
                            set(nx, ny, nz, SEEN); long q = pack(nx, ny, nz); queue.enqueue(q); touched.add(q);
                        }
                    }
                    if (size > bestSize) { bestSize = size; start = from; }
                }
            }
            for (int i = 0; i < touched.size(); i++) {
                long p = touched.getLong(i);
                chunks.get(key(packX(p) >> 4, packZ(p) >> 4))[((packZ(p) & 15) * 16 + (packX(p) & 15)) * H + (packY(p) - MIN_Y)] &= ~SEEN;
            }
        }
        if (start < 0) return "0 0 0 0 0 0 0 0 at 0 0 0 chunks " + chunks.size() + " seconds " + String.format("%.1f", (System.nanoTime() - t0) / 1e9);

        // the flood fill; cells whose chunk has no core yet wait until the chunks around are made
        LongArrayFIFOQueue queue = new LongArrayFIFOQueue(); LongArrayList waiting = new LongArrayList(); LongOpenHashSet visited = new LongOpenHashSet();
        set(packX(start), packY(start), packZ(start), SEEN); queue.enqueue(start);
        long core = 0; double sumX = 0, sumY = 0, sumZ = 0;
        int loX = Integer.MAX_VALUE, hiX = Integer.MIN_VALUE, loZ = Integer.MAX_VALUE, hiZ = Integer.MIN_VALUE;
        while (true) {
            while (!queue.isEmpty()) {
                long p = queue.dequeueLong(); core++;
                int px = packX(p), py = packY(p), pz = packZ(p);
                sumX += px; sumY += py; sumZ += pz; visited.add(key(px >> 4, pz >> 4));
                for (int d = 0; d < 6; d++) {
                    int nx = px + (d == 0 ? 1 : d == 1 ? -1 : 0), ny = py + (d == 2 ? 1 : d == 3 ? -1 : 0), nz = pz + (d == 4 ? 1 : d == 5 ? -1 : 0);
                    if (ny < MIN_Y || ny >= MIN_Y + H) continue;
                    if (!withCore.contains(key(nx >> 4, nz >> 4))) { waiting.add(pack(nx, ny, nz)); continue; }
                    if ((get(nx, ny, nz) & (CORE | SEEN)) != CORE) continue;
                    set(nx, ny, nz, SEEN); queue.enqueue(pack(nx, ny, nz));
                }
            }
            if (waiting.isEmpty()) break;
            LongOpenHashSet need = new LongOpenHashSet();
            for (int i = 0; i < waiting.size(); i++) {
                long p = waiting.getLong(i); int wx = packX(p) >> 4, wz = packZ(p) >> 4;
                for (int dz = -GROW; dz <= GROW; dz++) for (int dx = -GROW; dx <= GROW; dx++) if (!withCore.contains(key(wx + dx, wz + dz))) need.add(key(wx + dx, wz + dz));
            }
            ensureCore(need);
            LongArrayList again = waiting; waiting = new LongArrayList();
            for (int i = 0; i < again.size(); i++) {
                long p = again.getLong(i); int nx = packX(p), ny = packY(p), nz = packZ(p);
                if ((get(nx, ny, nz) & (CORE | SEEN)) != CORE) continue;
                set(nx, ny, nz, SEEN); queue.enqueue(p);
            }
        }

        // the air the cube sweeps: within R of the core that was reached; the chunks around the visited ones count too
        LongOpenHashSet sweep = new LongOpenHashSet();
        for (long k : visited) for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) sweep.add(key(keyX(k) + dx, keyZ(k) + dz));
        long[] totals = new long[1]; int[] box = {Integer.MAX_VALUE, Integer.MIN_VALUE, Integer.MAX_VALUE, Integer.MIN_VALUE, Integer.MAX_VALUE, Integer.MIN_VALUE};
        double mx = sumX / core, my = sumY / core, mz = sumZ / core; double[] nearest = {Double.MAX_VALUE}; int[] stand = new int[3];
        List<Runnable> jobs = new ArrayList<>();
        for (long k : sweep) jobs.add(() -> {
            byte[] cells = chunks.get(k);
            if (cells == null) return;
            int cx = keyX(k), cz = keyZ(k);
            byte[] swept = filtered(cx, cz, SEEN, false);
            long count = 0; int[] b = {Integer.MAX_VALUE, Integer.MIN_VALUE, Integer.MAX_VALUE, Integer.MIN_VALUE, Integer.MAX_VALUE, Integer.MIN_VALUE}; double near = Double.MAX_VALUE; int[] here = new int[3];
            for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
                int from = ((z + R) * P + (x + R)) * PH + R, to = (z * 16 + x) * H;
                for (int y = 0; y < H; y++) {
                    if (swept[from + y] == 0 || (cells[to + y] & AIR) == 0) continue;
                    count++;
                    int wx = (cx << 4) + x, wy = MIN_Y + y, wz = (cz << 4) + z;
                    b[0] = Math.min(b[0], wx); b[1] = Math.max(b[1], wx); b[2] = Math.min(b[2], wy); b[3] = Math.max(b[3], wy); b[4] = Math.min(b[4], wz); b[5] = Math.max(b[5], wz);
                    if ((cells[to + y] & SEEN) != 0) {
                        double d = (wx - mx) * (wx - mx) + (wy - my) * (wy - my) + (wz - mz) * (wz - mz);
                        if (d < near) { near = d; here[0] = wx; here[1] = wy; here[2] = wz; }
                    }
                }
            }
            synchronized (totals) {
                totals[0] += count;
                for (int i = 0; i < 6; i += 2) { box[i] = Math.min(box[i], b[i]); box[i + 1] = Math.max(box[i + 1], b[i + 1]); }
                if (near < nearest[0]) { nearest[0] = near; System.arraycopy(here, 0, stand, 0, 3); }
            }
        });
        parallel(jobs);
        return totals[0] + " " + core + " " + box[0] + " " + box[1] + " " + box[2] + " " + box[3] + " " + box[4] + " " + box[5] + " at " + stand[0] + " " + stand[1] + " " + stand[2]
            + " chunks " + chunks.size() + " seconds " + String.format("%.1f", (System.nanoTime() - t0) / 1e9);
    }

    public static void main(String[] args) throws Exception {
        threads = Integer.parseInt(args[0]);
        CaveDump.setup();
        BufferedReader reader = new BufferedReader(new InputStreamReader(System.in));
        for (String line; (line = reader.readLine()) != null; ) {
            String[] w = line.trim().split("\\s+", 5);
            if (w.length < 4) continue;
            seed = Long.parseLong(w[0]);
            int x = (int) Double.parseDouble(w[1]), y = (int) Double.parseDouble(w[2]), z = (int) Double.parseDouble(w[3]);
            Bootstrap.STDOUT.println(w[0] + " " + x + " " + z + " " + measure(x, y, z) + " | " + (w.length > 4 ? w[4] : ""));
            Bootstrap.STDOUT.flush();
        }
    }
}
