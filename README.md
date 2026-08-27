# Rill

A small dynamically typed language with an expression-oriented core, compiled
to bytecode and run on a stack virtual machine. Written from scratch in C++17
with no third-party libraries.

```
lexer -> Pratt parser -> single-pass bytecode compiler -> stack VM
```

There is no AST. The parser calls the compiler's emit functions directly as it
parses, which is what "single-pass" means here.

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
can appear in expression position, a local's slot is not its index in the
locals array, but the depth its initializer landed on.

## Language tour

```rill
# Bindings. `let` is immutable, and assigning to one is a COMPILE error.
let limit = 10
var count = 0
count = count + 1

# Functions are anonymous expressions. The last expression is the return
# value; `return` exists for early exit.
let square = fn(x) { x * x }
let clamp = fn(x, lo, hi) {
  if x < lo { return lo; }
  if x > hi { return hi; }
  x
}

# Recursion.
let fib = fn(n) { if n < 2 { n } else { fib(n - 1) + fib(n - 2) } }

# Closures capture by reference, and two closures over the same variable
# share it.
let makeCounter = fn() {
  var n = 0;
  fn() { n = n + 1; n }
}
let c = makeCounter()
print(c())   # 1
print(c())   # 2

# Loops. `while` yields nil; break and continue work as expected.
var i = 0
while i < 3 { print(i); i = i + 1; }

# `and` and `or` short-circuit and yield an operand, not a coerced boolean.
print(nil or 3)    # 3

# Builtins: print, clock, sqrt, floor, len, str, num
print(sqrt(16))    # 4
```

Semicolons are optional. A `;` discards the preceding expression; two
expressions may also just follow one another, in which case the earlier one is
discarded anyway. The cost is that a line beginning with `-` or `(` continues
the previous line — write the `;` to disambiguate.

## Building

Everything builds and runs inside Docker, so the local environment matches CI
and valgrind is available regardless of host OS.

```bash
docker compose build
docker compose run --rm dev bash scripts/dev.sh all     # build + test
docker compose run --rm dev ./build/rill program.rl     # run a file
docker compose run --rm dev ./build/rill                # REPL
```

Build options, all off unless noted: `RILL_GC_STRESS`, `RILL_NANBOX`,
`RILL_COMPUTED_GOTO`, `RILL_TRACE`, and `RILL_FOLD` / `RILL_SUPEROPS` (on).

## Testing

- **Behavior tests** — `.rl` files carrying their own expectations inline
  (`# expect: 5`, `# expect error: ...`), run by `tests/run_behavior_tests.py`.
- **Unit tests** — a ~30-line assertion harness, since no third-party
  libraries means no gtest or Catch2.
- **CI** — GitHub Actions across {gcc, clang} x {Debug, Release}.

```bash
docker compose run --rm dev valgrind --leak-check=full --error-exitcode=1 \
  ./build/rill_tests
```

## Status

Phase 1 is complete: lexer, Pratt parser, single-pass compiler, stack VM,
variables with compile-time immutability, blocks as expressions, control flow,
functions, native builtins, and closures with upvalue capture. 72 behavior
tests and 40 unit tests pass on all four CI configurations, valgrind-clean.

**Not yet implemented:** `match` expressions, the garbage collector (objects
are freed only at exit), prototype objects and property access, the measured
optimizations, the CPython benchmark suite, and the time-travel debugger.

Design and plans live in [docs/superpowers/](docs/superpowers/).
