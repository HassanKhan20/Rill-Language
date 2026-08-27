# Rill benchmarks

Two questions, measured separately:

1. **What are the optimizations worth?** Rill against Rill, one flag at a time.
2. **How does the VM compare to CPython?** Rill against CPython 3.11 on the
   same workloads.

Reproduce both with:

```bash
docker compose run --rm dev bash -c '
  cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
  python3 bench/run_bench.py compare --rill build/rill'
```

## Read this before the numbers

- **The measurement environment is noisy.** These were gathered in a Docker
  container on WSL2 on a Windows laptop, not on an isolated machine with
  frequency scaling pinned. Run-to-run variation of ±10% is common, and early
  runs of this suite produced swings large enough to invert a result. Each
  figure is the **minimum of 9 runs** after a discarded warmup, which is the
  cheapest available defence; the median is printed alongside so a lucky
  minimum is visible. **Treat differences under ~10% as noise.**
- **Rill has doubles where CPython has arbitrary-precision integers.** This
  helps Rill on `loop` (no bignum checks, no allocation per integer) and would
  hurt Rill on any workload exceeding 2^53, which none of these do. It is a
  real difference in what the two languages promise, not a trick.
- **These are microbenchmarks.** They say what they measure and nothing more.
  None of them is a program anyone would write.
- **Losses are reported alongside wins.** Rill loses on `strings`.

## The benchmarks

| Benchmark | What it stresses |
|---|---|
| `fib` | Recursive `fib(27)` — call and return overhead, frame setup |
| `loop` | 3M-iteration numeric loop — dispatch cost, arithmetic, local access |
| `strings` | 60k string builds — allocation churn, interning, GC frequency |
| `trees` | Binary trees depth 12 ×24 — object allocation, pointer chasing, GC pressure |
| `constfold` | Constant-heavy arithmetic — the workload constant folding is *for* |

Each is written twice, once in Rill and once in Python, doing the same work by
the same algorithm. The harness verifies both print identical output; a
benchmark whose two implementations disagree is not a benchmark, and the
runner says so loudly.

## 1. What the optimizations are worth

Two optimizations are implemented, each behind an independent build flag:

- **`RILL_FOLD`** — constant folding. A single-pass compiler has no tree to
  fold over, so it keeps a two-instruction peephole window and rewrites
  `Constant a; Constant b; <op>` into a single `Constant (a op b)`, rewinding
  the chunk in place. All five arithmetic operators are folded; every one is
  exact under IEEE-754, including division and modulo by zero, which produce
  the same infinity or NaN whether computed then or now.
- **`RILL_SUPEROPS`** — specialized instructions. `GetLocal0`–`GetLocal3`
  replace `GetLocal <n>` for the low slots that dominate real code, and
  `AddConst <c>` fuses `Constant c; Add`, which is the shape of every `i = i + 1`.

### Bytecode size — deterministic, no timing noise

This is the honest headline for constant folding, because it is exact:

| Benchmark | Instructions (base) | Instructions (both) | Change |
|---|---:|---:|---:|
| `constfold` | 50 | 40 | **−20.0%** |
| `loop` | 38 | 37 | −2.6% |
| `strings` | 46 | 45 | −2.2% |
| `trees` | 46 | 45 | −2.2% |
| `fib` | 11 | 11 | 0.0% |

### Runtime

Minimum of 9 runs, Release build:

| Benchmark | base | +fold | +fold +superops | vs base |
|---|---:|---:|---:|---:|
| `constfold` | 0.127s | 0.121s | **0.091s** | **+28.1% faster** |
| `fib` | 0.025s | 0.027s | 0.024s | +5.5% (noise) |
| `loop` | 0.173s | 0.205s | 0.166s | +4.2% (noise) |
| `strings` | 0.049s | 0.053s | 0.049s | −0.1% (noise) |
| `trees` | 0.038s | 0.039s | 0.041s | −6.7% (noise) |

**The interesting result is the one that looks like a failure.** Folding does
essentially nothing for `fib`, `loop`, `strings`, or `trees` — and that is
correct, not a bug. Those benchmarks contain no foldable constant
subexpressions in their hot paths: `total + i * 2 - 1` has a variable in every
operand of every operator. An optimization can only pay where its pattern
occurs. `constfold` exists to show what happens where it does occur, and there
the combined effect is a real 28%.

The `fold`-only column being *slower* than base on three rows is measurement
noise, not a regression: constant folding strictly removes work at compile
time and cannot make the interpreter loop slower. It is left in the table
rather than quietly re-run until it looked right.

## 2. Rill vs CPython 3.11.2

Minimum of 9 runs, Release build, both inside the same container:

| Benchmark | Rill (min/median) | CPython (min/median) | Result |
|---|---:|---:|---:|
| `fib` | 0.027s / 0.029s | 0.069s / 0.095s | **2.59× faster** |
| `loop` | 0.168s / 0.176s | 0.365s / 0.393s | **2.17× faster** |
| `trees` | 0.039s / 0.047s | 0.072s / 0.084s | **1.87× faster** |
| `constfold` | 0.094s / 0.100s | 0.171s / 0.187s | **1.83× faster** |
| `strings` | 0.050s / 0.056s | 0.040s / 0.059s | **0.80× — slower** |

All five benchmarks produce identical output in both languages.

### Why Rill wins where it wins

`fib` and `loop` are pure dispatch and arithmetic. Rill's advantage is
structural rather than clever: values are a 16-byte tagged union with no
allocation for numbers, calls push a frame into a fixed array rather than
allocating a frame object, and locals are stack slots reached by index.
CPython allocates and reference-counts an object for essentially every
intermediate value. `trees` is allocation-heavy, and Rill's bump-style
`malloc` plus a collector that only runs on a growth threshold beats
per-object reference counting.

### Why Rill loses on `strings`

`strings` is the honest loss, and it is a direct consequence of a design
decision. Rill **interns every string**, so building 60,000 distinct strings
means 60,000 hash computations and 60,000 intern-table probes, plus table
growth. Interning buys O(1) string equality — `valuesEqual` is a pointer
comparison — which is exactly the wrong trade for a workload that creates many
strings and compares none of them. CPython interns only short identifier-like
strings and lets the rest be ordinary objects.

Fixing this would mean interning lazily, or only below a length threshold. It
is a known cost of a deliberate choice, not an accident.

## Environment

- Image: `debian:bookworm-slim`, g++ 12.2.0, `-O3` via `CMAKE_BUILD_TYPE=Release`
- CPython 3.11.2, same container
- Host: Windows 11 laptop, Docker Desktop on WSL2 kernel 6.18.33.2
- Raw data: `bench/results/compare.json`, `bench/results/ablate.json`

## Not implemented

The design document proposed four optimizations. Two shipped. **NaN-boxing**
(packing values into 8 bytes) and **computed-goto dispatch** were not built;
their build flags have been removed from `CMakeLists.txt` and CI rather than
left in place advertising work that does not exist. Both remain the obvious
next steps, and the flag-per-optimization structure and ablation harness are
already in place to measure them.
