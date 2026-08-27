# Rill

A small dynamically typed language with an expression-oriented core, compiled
to bytecode and run on a stack virtual machine with a mark-and-sweep garbage
collector. Written from scratch in C++17, no third-party libraries.

```
lexer → Pratt parser → single-pass bytecode compiler → stack VM
```

There is no AST. The parser calls the compiler's emit functions directly as it
parses — that is what "single-pass" means here, and it is the constraint that
shapes most of the interesting decisions below.

## The idea

Everything is an expression, and a block's value is its final expression.

```rill
let sign = if n > 0 { "pos" } else { "neg" }   # if yields a value
let y = { let t = f(x); t * t }                # so does a block
let z = { g(); }                               # a trailing ; yields nil
```

That one rule is the spine of the implementation. It forces a central compiler
invariant — **every construct leaves exactly one value on the stack** — and it
is why the compiler tracks the stack depth it is emitting at: because a block
can appear in expression position, a local's slot is *not* its index in the
locals array (as it is in a statement-oriented language), but the depth its
initializer happened to land on.

## Language tour

```rill
# `let` is immutable — assigning to one is a COMPILE error, not a runtime one.
let limit = 10
var count = 0
count = count + 1

# Functions are anonymous expressions. The body is a block, so the last
# expression is the return value; `return` exists for early exit.
let square = fn(x) { x * x }
let fib = fn(n) { if n < 2 { n } else { fib(n - 1) + fib(n - 2) } }

# match: literal patterns, | alternatives, guards, wildcards, and bindings.
let describe = fn(v) {
  match v {
    0          -> "zero",
    1 | 2      -> "small",
    n if n < 0 -> "negative",
    _          -> "big"
  }
}

# Closures capture by reference, and two closures over the same variable
# share it.
let makeCounter = fn() { var n = 0; fn() { n = n + 1; n } }
let c = makeCounter()
print(c())   # 1
print(c())   # 2

# No classes. Objects are maps with a prototype link; a method call passes the
# receiver as the first argument, so there is no `this`.
let Point = {
  x: 0, y: 0,
  norm: fn(self) { sqrt(self.x * self.x + self.y * self.y) }
}
let p = clone(Point)
p.x = 3
p.y = 4
print(p.norm())   # 5

# Loops. `while` yields nil; break and continue work as expected.
var i = 0
while i < 3 { print(i); i = i + 1; }

# and/or short-circuit and yield an operand, not a coerced boolean.
print(nil or 3)   # 3
```

Semicolons are optional: a `;` discards the preceding expression, and two
expressions may simply follow one another, in which case the earlier one is
discarded anyway. The cost is that a line beginning with `-` or `(` continues
the previous line — write the `;` to disambiguate. Lua makes the same trade.

Builtins: `print`, `clock`, `sqrt`, `floor`, `len`, `str`, `num`, `clone`,
`has`, `gc`, `gcCount`.

## Garbage collector

Mark-and-sweep with **precise roots** — no conservative stack scanning:

- Every heap object begins with a common header and is threaded onto an
  intrusive allocation list. `allocateObject` is the single choke point.
- Roots are enumerated exactly: the VM walks its value stack, call frames,
  globals, and open upvalues; the compiler walks its chain of functions under
  construction; and a **temp-root stack** covers objects that have been
  allocated but not yet linked into the graph. That last one matters — an
  object reachable only from a C++ local is invisible to the collector, and
  it is the classic bug in a from-scratch collector.
- The string intern table holds **weak** references, swept after marking and
  before the sweep phase.
- `-DRILL_GC_STRESS=ON` collects before *every* allocation. The whole suite
  runs under it in CI, which turns "mysterious crash in three weeks" into
  "fails on the test that introduced it."

## Performance

Two measured optimizations, each behind an independent build flag:

- **`RILL_FOLD`** — constant folding via a two-instruction peephole window,
  since a single-pass compiler has no tree to fold over.
- **`RILL_SUPEROPS`** — `GetLocal0`–`GetLocal3` and `AddConst`.

Against CPython 3.11 on five microbenchmarks: **1.8–2.6× faster on four**,
and **slower on string building**, because Rill interns every string and that
is the wrong trade for a workload that builds many strings and compares none.

Full numbers, methodology, and the noise caveats are in
[docs/benchmarks.md](docs/benchmarks.md). The most interesting result there is
that folding does nothing for four of the five benchmarks — correctly, because
they contain no foldable constant subexpressions.

## Building

Everything builds and runs inside Docker, so the local environment matches CI
and valgrind is available regardless of host OS.

```bash
docker compose build
docker compose run --rm dev bash scripts/dev.sh all     # build + test
docker compose run --rm dev ./build/rill program.rl     # run a file
docker compose run --rm dev ./build/rill                # REPL
docker compose run --rm dev ./build/rill --dump prog.rl # disassemble
docker compose run --rm dev ./build/rill --debug prog.rl # time-travel debugger
```

Build options: `RILL_GC_STRESS`, `RILL_TRACE` (off), `RILL_FOLD`,
`RILL_SUPEROPS` (on).

## Testing

- **120 behavior tests** — `.rl` files carrying their expectations inline
  (`# expect: 5`, `# expect error: ...`), run by `tests/run_behavior_tests.py`.
- **40 unit tests** — a ~30-line assertion harness; no third-party libraries
  means no gtest or Catch2.
- **7 debugger tests** — `tests/test_debugger.py` drives the REPL and checks
  that backward stepping restores state, including under GC stress.
- **CI** — {gcc, clang} × {Debug, Release}, plus GC-stress, valgrind over the
  entire behavior suite, ASan/UBSan, an optimization matrix verifying every
  flag combination produces identical output, the debugger tests, and a guard
  that fails the build if the test count drops below 100.

```bash
docker compose run --rm dev valgrind --leak-check=full --error-exitcode=1 \
  ./build/rill_tests
```

## Time-travel debugger

`rill --debug program.rl` records execution as an undo log and steps
**backward** through it. Stepping back far enough undoes a global's definition,
not merely its last value; call frames and the value stack are restored too.

```
(rill-dbg) back 6
[line 3] step 7
(rill-dbg) print counter
counter = 1
```

Recording costs **24–127× slowdown at 136 bytes per instruction**, and the log
is **unbounded** — 9 GB for a 36M-instruction program. It is correct but only
practical for short programs. [docs/debugger.md](docs/debugger.md) explains the
design, the measurements, and the keyframe-plus-replay fix that would bound
memory.

## Status

Complete: the pipeline, variables with compile-time immutability, blocks as
expressions, control flow, functions, closures with upvalue capture, `match`,
the garbage collector, prototype objects, two measured optimizations, the
CPython benchmark suite, CI, and the time-travel debugger. Valgrind-clean
under GC stress.

**Not implemented:** NaN-boxing and computed-goto dispatch. Their build flags
were removed rather than left advertising work that does not exist.

Design and plans: [docs/superpowers/](docs/superpowers/).
