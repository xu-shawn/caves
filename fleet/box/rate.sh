#!/bin/bash
# What the box is doing now, over the next SECONDS (default 180): the gate's looks per second (from two lines of its log,
# which give averages since its start), the workers' seeds per minute, the CPU threads in use and the time the container
# was held back by its quota, the top seeds the gate lists per minute (final score <= TOP, default -0.62: what the looks are
# for, and comparable between gate settings that change what a look is worth), and the yield index T^0.6 W^0.4 the settings
# are compared by (T top seeds/min, W seeds/min).
cd /root/caves || exit 1
N=${1:-180}; TOP=${TOP:--0.62}
line() { tail -n 1 runs/g14/gate.log | awk '{t = $1; for (i = 1; i <= NF; i++) if ($i == "from" && $(i+1) == "the" && $(i+2) == "scan,") { v = $(i-1); sub("/s", "", v) } print t, v}'; }
cpu() {   # microseconds of CPU used and of being held back, cgroup v2 or v1
    if [ -f /sys/fs/cgroup/cpu.stat ]; then awk '/^usage_usec/ {u = $2} /^throttled_usec/ {t = $2} END {print u, t}' /sys/fs/cgroup/cpu.stat
    else echo "$(( $(cat /sys/fs/cgroup/cpu,cpuacct/cpuacct.usage) / 1000 )) $(awk '/^throttled_time/ {print int($2 / 1000)}' /sys/fs/cgroup/cpu,cpuacct/cpu.stat)"; fi; }
read t0 v0 <<< "$(line)"; s0=$(wc -l < runs/g14/seeds.txt); g0=$(wc -l < runs/g14/gate.txt); read u0 h0 <<< "$(cpu)"; c0=$(date +%s)
until [ $(( $(date +%s) - c0 )) -ge $N ]; do sleep 5; done
read t1 v1 <<< "$(line)"; s1=$(wc -l < runs/g14/seeds.txt); top=$(tail -n +$((g0 + 1)) runs/g14/gate.txt | awk -v t=$TOP '$2 <= t' | wc -l); all=$(( $(wc -l < runs/g14/gate.txt) - g0 )); read u1 h1 <<< "$(cpu)"; c1=$(date +%s)
awk -v t0=$t0 -v v0=$v0 -v t1=$t1 -v v1=$v1 -v s0=$s0 -v s1=$s1 -v u0=$u0 -v u1=$u1 -v h0=$h0 -v h1=$h1 -v dt=$((c1 - c0)) -v w=$(pgrep -x python3 | wc -l) -v top=$top -v all=$all 'BEGIN {
    L = (t1 > t0) ? (v1 * t1 - v0 * t0) / (t1 - t0) : 0; W = (s1 - s0) * 60 / dt;
    T = top * 60 / dt;
    printf "%d s, %d workers: %.0f looks/s, %.0f listed/min of which top %.0f/min, %.0f seeds/min, %.1f CPU threads, held back %.1f%% of the time; index %.1f\n", dt, w, L, all * 60 / dt, T, W, (u1 - u0) / 1e6 / dt, 100 * (h1 - h0) / 1e6 / dt, (T > 0 && W > 0) ? exp(0.6 * log(T) + 0.4 * log(W)) : 0 }'
