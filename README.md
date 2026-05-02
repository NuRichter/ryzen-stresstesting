# ryzen-socket-breach

push your 9600X until it taps out. or doesn't.

binary-stack-dispatched stress workloads across all 12 threads of your AM5 socket, with live thermal + IPC readout via ncurses. targets Zen 5 specifically -- avx2 fma blasts, cache thrashing, branch predictor torture, integer hammer chains, all of it.

**Arch Linux only.** no plans to support anything else.

---

## what it does

- lock-free binary stack (`WorkItem` packed into 64 bits) feeds workloads to per-core worker threads
- dispatcher thread refills the stack whenever it runs dry
- six stress kernels: `integer`, `float_avx`, `cache_l1`, `cache_l2`, `branch`, `combined`
- reads `k10temp` via hwmon sysfs for Tdie / Tctl / package wattage
- `perf_event_open` for cycles, instructions, cache refs/misses, branch misses per core
- optional ryzenadj PPT/TDC/EDC override to push past AMD's stock 65W limits
- ncurses TUI with per-core op bars + thermal color coding, or `--no-tui` for plain stdout

---

## build

```bash
git clone https://github.com/NuRichter/ryzen-socket-breach
cd ryzen-socket-breach
bash scripts/arch_setup.sh
```

or manually:

```bash
sudo pacman -Sy --needed base-devel cmake ninja ncurses ryzenadj linux-headers
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

> if `ryzenadj` isn't found, power-limit features are silently disabled and everything else still works fine.

---

## usage

```
./build/rsb [options]

  -t <sec>    duration in seconds       (default: 30)
  -c <n>      logical core count        (default: 12)
  -q <n>      stack queue depth         (default: 512)
  --ppt       push PPT/TDC/EDC limits   (root required)
  --no-tui    plain stdout output
  -h          help
```

**standard run:**
```bash
./build/rsb -t 60
```

**push power limits (needs root + ryzenadj):**
```bash
sudo ./build/rsb -t 120 --ppt
```

**headless / logging:**
```bash
sudo ./build/rsb -t 300 --ppt --no-tui | tee stress.log
```

**perf counters without root:**
```bash
sudo sysctl -w kernel.perf_event_paranoid=0
./build/rsb -t 60
```

---

## the binary stack

`WorkItem` is 64 bits:

```
[63:56] type   -- stress kernel (8 possible)
[55:48] core   -- target logical CPU
[47:32] iter   -- inner loop multiplier (x1024)
[31: 0] seed   -- PRNG seed for data pattern
```

the stack is a Michael-Scott style lock-free design over a pre-allocated node pool. no heap allocs after init. dispatcher pushes one item per core whenever the stack drains, workers pop and execute.

---

## TUI

```
 ryzen-socket-breach // 9600X AM5 stress
 Tdie: 84.3 C  Tctl: 84.3 C  PKG: 88.1 W
 Ops: 14823  Elapsed: 18/60 s
-------------------------------------------------
 CPU  ops       IPC     miss%   bmiss  load
 C0   1240      3.81    0.42    18291  ############.........
 C1   1198      3.79    0.38    17002  ###########..........
 ...
```

green = under 65C, yellow = 65-80C, red = above 80C.

---

## notes

- `--ppt` sets PPT to 95W, TDC/EDC to 75A/95A. adjust in `stress_engine.hpp` if you want different numbers
- thermal readout depends on `k10temp` kernel module being loaded (`modprobe k10temp`)
- perf counters need `kernel.perf_event_paranoid <= 0` or root
- no stability guarantees. you're pushing a CPU past its rated limits. that's the point

---

## license

MIT
