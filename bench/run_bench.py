#!/usr/bin/env python3
"""Benchmark the Rill VM against CPython, and Rill against itself.

Two modes:

    run_bench.py compare --rill build/rill
        Rill vs CPython on each microbenchmark.

    run_bench.py ablate --builds base=build-base fold=build-fold ...
        The same benchmarks across several Rill builds, to measure what an
        individual optimization is worth.

Every benchmark is run REPS times after a discarded warmup; the reported
figure is the minimum, which is the least noisy estimator for CPU-bound work,
with the median shown alongside so a suspiciously lucky minimum is visible.
"""

import argparse
import json
import pathlib
import platform
import statistics
import subprocess
import sys
import time

BENCHMARKS = [
    ("fib", "Recursive fib(27): call and return overhead"),
    ("loop", "3M-iteration numeric loop: dispatch and arithmetic"),
    ("strings", "60k string builds: allocation churn and GC frequency"),
    ("trees", "Binary trees depth 12 x24: allocation and pointer chasing"),
    ("constfold", "Constant-heavy arithmetic: the folding workload"),
]

# This project is developed in a container on WSL2, which is a noisy place to
# measure. More repetitions and reporting the minimum is the cheapest defence;
# the median is printed alongside so a lucky minimum is visible.
REPS = 9
HERE = pathlib.Path(__file__).resolve().parent


def time_command(cmd, reps=REPS):
    """Returns (min, median, stdout) or (None, None, error) if the run fails."""
    # Warmup, also validating that the command works at all.
    warm = subprocess.run(cmd, capture_output=True, text=True)
    if warm.returncode != 0:
        return None, None, (warm.stderr or "").strip() or "nonzero exit"

    times = []
    for _ in range(reps):
        start = time.perf_counter()
        proc = subprocess.run(cmd, capture_output=True, text=True)
        elapsed = time.perf_counter() - start
        if proc.returncode != 0:
            return None, None, (proc.stderr or "").strip() or "nonzero exit"
        times.append(elapsed)

    return min(times), statistics.median(times), warm.stdout.strip()


def fmt(seconds):
    return "-" if seconds is None else f"{seconds:8.3f}s"


def cmd_compare(args):
    rill = pathlib.Path(args.rill).resolve()
    python = args.python

    rows = []
    print(f"Rill:    {rill}")
    print(f"CPython: {subprocess.run([python, '--version'], capture_output=True, text=True).stdout.strip()}")
    print(f"Machine: {platform.platform()} / {platform.processor() or 'unknown cpu'}")
    print(f"Reps:    {REPS} (reporting minimum; median in parentheses)")
    print()

    for name, description in BENCHMARKS:
        r_min, r_med, r_out = time_command([str(rill), str(HERE / "rill" / f"{name}.rl")])
        p_min, p_med, p_out = time_command([python, str(HERE / "python" / f"{name}.py")])

        # A benchmark whose two implementations disagree is not a benchmark.
        agree = (r_out == p_out) if (r_min and p_min) else False
        ratio = (p_min / r_min) if (r_min and p_min) else None

        rows.append({
            "benchmark": name,
            "description": description,
            "rill_min": r_min, "rill_median": r_med,
            "python_min": p_min, "python_median": p_med,
            "speedup_vs_cpython": ratio,
            "outputs_agree": agree,
            "rill_output": r_out, "python_output": p_out,
        })

    width = max(len(n) for n, _ in BENCHMARKS)
    print(f"{'benchmark'.ljust(width)}  {'rill':>18}  {'cpython':>18}  {'ratio':>9}  agree")
    print("-" * (width + 60))
    for row in rows:
        rill_cell = f"{fmt(row['rill_min'])} ({fmt(row['rill_median']).strip()})"
        py_cell = f"{fmt(row['python_min'])} ({fmt(row['python_median']).strip()})"
        if row["speedup_vs_cpython"] is None:
            ratio = "  -"
        else:
            r = row["speedup_vs_cpython"]
            ratio = f"{r:.2f}x {'faster' if r >= 1 else 'SLOWER'}"
        print(f"{row['benchmark'].ljust(width)}  {rill_cell:>18}  {py_cell:>18}  {ratio:>16}  "
              f"{'yes' if row['outputs_agree'] else 'NO'}")

    for row in rows:
        if not row["outputs_agree"]:
            print(f"\n!! {row['benchmark']}: outputs differ "
                  f"(rill={row['rill_output']!r} python={row['python_output']!r})")

    write_json(args.json, {"mode": "compare", "reps": REPS, "rows": rows})
    return 0


def cmd_ablate(args):
    builds = []
    for spec in args.builds:
        label, _, path = spec.partition("=")
        builds.append((label, pathlib.Path(path).resolve()))

    print(f"Reps: {REPS} (reporting minimum)")
    print()

    results = {}
    for name, _ in BENCHMARKS:
        results[name] = {}
        for label, binary in builds:
            m, med, out = time_command([str(binary), str(HERE / "rill" / f"{name}.rl")])
            results[name][label] = {"min": m, "median": med, "output": out}

    labels = [label for label, _ in builds]
    width = max(len(n) for n, _ in BENCHMARKS)
    header = "  ".join(f"{lab:>12}" for lab in labels)
    print(f"{'benchmark'.ljust(width)}  {header}   vs {labels[0]}")
    print("-" * (width + 16 * len(labels) + 12))

    for name, _ in BENCHMARKS:
        cells = []
        base = results[name][labels[0]]["min"]
        for lab in labels:
            m = results[name][lab]["min"]
            cells.append(f"{fmt(m):>12}")
        last = results[name][labels[-1]]["min"]
        delta = ""
        if base and last:
            change = (base - last) / base * 100.0
            delta = f"{change:+6.1f}%"
        print(f"{name.ljust(width)}  {'  '.join(cells)}   {delta}")

    # Every build must agree on every result, or an "optimization" changed
    # program behaviour.
    for name, _ in BENCHMARKS:
        outs = {results[name][lab]["output"] for lab in labels}
        if len(outs) > 1:
            print(f"\n!! {name}: builds disagree on output: {outs}")

    write_json(args.json, {"mode": "ablate", "reps": REPS, "results": results})
    return 0


def write_json(path, payload):
    if not path:
        return
    out = pathlib.Path(path)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(payload, indent=2), encoding="utf-8")
    print(f"\nwrote {out}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)

    c = sub.add_parser("compare", help="Rill vs CPython")
    c.add_argument("--rill", default="build/rill")
    c.add_argument("--python", default=sys.executable)
    c.add_argument("--json", default="")
    c.set_defaults(func=cmd_compare)

    a = sub.add_parser("ablate", help="Rill build vs Rill build")
    a.add_argument("--builds", nargs="+", required=True,
                   metavar="LABEL=PATH")
    a.add_argument("--json", default="")
    a.set_defaults(func=cmd_ablate)

    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
