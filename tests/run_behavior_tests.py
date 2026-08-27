#!/usr/bin/env python3
"""Run every tests/lang/**/*.rl and check output against inline annotations.

Each test file carries its own expectations:

    # expect: 5
    print(2 + 3)

    # expect error: undefined variable 'nope'
    print(nope)

A file with any `# expect error:` annotation must exit nonzero and its combined
stdout+stderr must contain every named substring. A file without one must exit
zero and its stdout must match the `# expect:` lines exactly, in order.
"""

import pathlib
import re
import subprocess
import sys

EXPECT = re.compile(r"#\s*expect:\s?(.*)$")
EXPECT_ERROR = re.compile(r"#\s*expect error:\s?(.*)$")

TIMEOUT_SECONDS = 120


def parse(path):
    """Extract expected stdout lines and expected error substrings."""
    out, errs = [], []
    for line in path.read_text(encoding="utf-8").splitlines():
        m = EXPECT_ERROR.search(line)
        if m:
            errs.append(m.group(1).strip())
            continue
        m = EXPECT.search(line)
        if m:
            out.append(m.group(1).rstrip())
    return out, errs


def run_one(binary, path, runner):
    want_out, want_errs = parse(path)
    cmd = runner + [str(binary), str(path)]
    try:
        proc = subprocess.run(
            cmd, capture_output=True, text=True, timeout=TIMEOUT_SECONDS
        )
    except subprocess.TimeoutExpired:
        return [f"timed out after {TIMEOUT_SECONDS}s"]

    got_out = [ln.rstrip() for ln in proc.stdout.splitlines()]
    combined = proc.stdout + proc.stderr

    problems = []
    if want_errs:
        for want in want_errs:
            if want not in combined:
                problems.append(
                    f"expected error containing {want!r}\n"
                    f"     got: {combined.strip()!r}"
                )
        if proc.returncode == 0:
            problems.append("expected a nonzero exit status, got 0")
    else:
        if proc.returncode != 0:
            problems.append(
                f"exit status {proc.returncode}\n     {proc.stderr.strip()}"
            )
        if got_out != want_out:
            problems.append(
                f"stdout mismatch\n     want: {want_out}\n     got:  {got_out}"
            )
    return problems


def main():
    if len(sys.argv) < 3:
        print(
            "usage: run_behavior_tests.py <rill-binary> <tests-dir> [runner...]",
            file=sys.stderr,
        )
        return 64

    binary = pathlib.Path(sys.argv[1]).resolve()
    root = pathlib.Path(sys.argv[2]).resolve()
    runner = sys.argv[3:]

    files = sorted(root.rglob("*.rl"))
    if not files:
        print(f"no .rl tests found under {root}", file=sys.stderr)
        return 1

    failed = 0
    for path in files:
        problems = run_one(binary, path, runner)
        if problems:
            failed += 1
            print(f"FAIL {path.relative_to(root)}")
            for p in problems:
                print(f"   {p}")

    print(f"{len(files) - failed}/{len(files)} behavior tests passed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
