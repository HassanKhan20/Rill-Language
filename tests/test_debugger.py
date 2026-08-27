#!/usr/bin/env python3
"""Drive the time-travel debugger and check that stepping backward restores
state. The behavior test runner only knows how to run a .rl file to
completion, so the debugger needs its own harness.
"""

import pathlib
import subprocess
import sys
import tempfile

FAILURES = []


def run_debugger(binary, source, commands):
    with tempfile.NamedTemporaryFile("w", suffix=".rl", delete=False,
                                     encoding="utf-8") as f:
        f.write(source)
        path = f.name
    try:
        proc = subprocess.run(
            [binary, "--debug", path],
            input="".join(c + "\n" for c in commands),
            capture_output=True, text=True, timeout=120,
        )
        return proc.stdout + proc.stderr
    finally:
        pathlib.Path(path).unlink(missing_ok=True)


def check(name, condition, detail=""):
    if condition:
        print(f"  ok   {name}")
    else:
        print(f"  FAIL {name}\n       {detail}")
        FAILURES.append(name)


def main():
    binary = sys.argv[1]

    # Stepping back must restore an overwritten global.
    out = run_debugger(binary, "var a = 0\na = 10\na = 20\n",
                       ["step 100", "print a", "back 3", "print a", "quit"])
    check("back restores an overwritten global",
          "a = 20" in out and "a = 10" in out, out)

    # Stepping back far enough must undo the global's definition entirely,
    # not merely its last value.
    out = run_debugger(binary, "var counter = 0\ncounter = 1\n",
                       ["step 100", "rewind", "print counter", "quit"])
    check("rewind undoes a global definition",
          "no global named 'counter'" in out, out)

    # Backward stepping must restore call frames, not just the value stack.
    out = run_debugger(binary, "let f = fn(n) { n * 2 }\nvar r = f(21)\n",
                       ["step 100", "print r", "back 4", "where", "quit"])
    check("forward run computes the right value", "r = 42" in out, out)
    check("back re-enters the callee's frame", "in f" in out, out)

    # A recorded run must reach the same end state as a normal run.
    out = run_debugger(binary, "var t = 0\nvar i = 0\nwhile i < 50 "
                               "{ t = t + i; i = i + 1; }\n",
                       ["step 100000", "print t", "quit"])
    check("loop replays to the same result", "t = 1225" in out, out)

    # Rewinding and replaying must be deterministic.
    out = run_debugger(binary, "var t = 0\nvar i = 0\nwhile i < 50 "
                               "{ t = t + i; i = i + 1; }\n",
                       ["step 100000", "rewind", "step 100000", "print t",
                        "quit"])
    check("rewind then replay reaches the same result", "t = 1225" in out, out)

    # The undo log must be a GC root: values it holds may be reachable from
    # nowhere else, and collecting them would corrupt a backward step.
    out = run_debugger(binary,
                       'var s = ""\nvar i = 0\n'
                       'while i < 200 { s = "x" + str(i); i = i + 1; }\n',
                       ["step 100000", "back 500", "step 500", "print s",
                        "quit"])
    check("undo log survives collection", "s = x199" in out, out)

    print(f"\n{6 - len(FAILURES)}/6 debugger tests passed")
    return 1 if FAILURES else 0


if __name__ == "__main__":
    sys.exit(main())
