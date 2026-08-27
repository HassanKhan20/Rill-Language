# Rill — Design Specification

**Date:** 2026-08-26
**Status:** Approved, pending implementation

Rill is a small dynamically typed language with an expression-oriented core,
implemented in C++17 with no third-party libraries. The implementation is a
four-stage pipeline — lexer, Pratt parser, single-pass bytecode compiler,
stack-based virtual machine — with a mark-and-sweep garbage collector,
measured bytecode optimizations, and a time-travel debugger.

## 1. Goals

1. A dynamically typed language of original design: variables, control flow,
   functions with closures, prototype-based objects.
2. Pipeline: lexer to Pratt parser to single-pass bytecode compiler to stack
   VM. C++17, no third-party libraries.
3. A mark-and-sweep garbage collector written from scratch, valgrind-clean.
4. At least one measured bytecode optimization with before/after benchmarks.
5. A benchmark suite comparing the Rill VM against CPython on four
   microbenchmarks.
6. 100+ language behavior tests running in GitHub Actions CI.
7. A time-travel debugger: record execution so a program can be stepped
   backward in a REPL, with measured snapshot overhead.

## 2. Non-Goals

- Performance parity with production VMs (LuaJIT, V8). The comparison target
  is CPython.
- A JIT compiler. Rill is an interpreter throughout.
- A module or import system. Programs are single files.
- Concurrency, async, or threads. The VM is single-threaded.
- Integer/float distinction, arbitrary-precision arithmetic, or a numeric
  tower. See section 4.4.
- Unicode-aware string operations. Strings are byte sequences; source is
  ASCII-oriented with UTF-8 passing through opaquely.

## 3. Development Environment

Development and CI both run inside Docker, so local results and CI results
come from the same image. Valgrind is Linux-only, which makes a Linux build
environment mandatory regardless of the host.

- Base image: `debian:bookworm-slim`, with `g++`, `clang`, `cmake`, `ninja`,
  `valgrind`, `python3`, and `git`.
- The same image is used by the local dev loop and by the GitHub Actions
  workflow.
- Host is Windows; the repository is bind-mounted into the container.

## 4. Language Design

### 4.1 The core rule

Everything is an expression. A block's value is its final expression. A `;`
evaluates its expression and discards the result.

```rill
let a = { let t = f(x); t * t }   # a == t*t
let b = { g(); }                  # trailing ; -> b == nil
let c = if n > 0 { "pos" } else { "neg" }
```

This rule is the language's spine and the source of the compiler's central
invariant: **every construct leaves exactly one value on the stack.**

A `;` is optional punctuation. Its only job is to discard, and two expressions
may simply follow one another, in which case the earlier one is discarded
anyway:

```rill
print(a)      # value discarded because another expression follows
print(b)      # the sequence's value
```

The cost of making `;` optional is that a line beginning with `-` or `(`
continues the previous line rather than starting a new expression: `a` newline
`-b` parses as `a - b`. Writing the `;` resolves it. This is the same tradeoff
Lua makes, and it is preferred here over mandatory semicolons because the
language is expression-oriented and most lines are values rather than
statements.

### 4.2 Bindings

`let` introduces an immutable binding; `var` introduces a mutable one.
Assigning to a `let` binding is a **compile-time** error, not a runtime one.

```rill
let limit = 10
var count = 0
count = count + 1   # ok
limit = 11          # compile error: cannot assign to immutable binding 'limit'
```

Redeclaring a name in the same scope is a compile error. Shadowing in a
nested scope is allowed.

### 4.3 Functions

Functions are anonymous expressions bound to names. The body is a block, so
the last expression is the return value; `return` is also available for early
exit.

```rill
let square = fn(x) { x * x }
let fact = fn(n) { if n <= 1 { 1 } else { n * fact(n - 1) } }
let clamp = fn(x, lo, hi) {
  if x < lo { return lo; }
  if x > hi { return hi; }
  x
}
```

Closures capture variables by reference through upvalues. Arity is checked at
call time and mismatches are runtime errors.

### 4.4 Values and types

Six runtime types: `nil`, `bool`, `number`, `string`, `function` (including
closures and builtins), and `map` (the object type, section 4.7).

Numbers are IEEE-754 doubles only. There is no integer type. This keeps
arithmetic and constant folding simple and uniform. The consequence for
benchmarking is that CPython's arbitrary-precision integers are a genuine
advantage for CPython on some workloads and a genuine disadvantage on others;
section 8.3 requires this be disclosed in the benchmark writeup rather than
papered over.

`nil` and `false` are the only falsey values. Everything else, including `0`
and the empty string, is truthy.

Equality (`==`) is by value for `nil`, `bool`, `number`, and `string`, and by
identity for `map` and `function`. Strings are interned, so string equality is
a pointer comparison at runtime.

### 4.5 Control flow

`if`/`else` is an expression. An `if` without an `else` yields `nil` when the
condition is falsey. Both branches must be blocks.

`while` is a statement-shaped expression that always yields `nil`. `break` and
`continue` are supported inside loops and are compile errors outside them.

Logical `and` and `or` short-circuit and yield one of their operands, not a
coerced boolean.

### 4.6 Match

`match` is an expression. Patterns are literals, `|` alternatives, a binding
identifier, or `_`. A binding pattern may carry a guard.

```rill
let describe = fn(v) {
  match v {
    0          -> "zero",
    1 | 2      -> "small",
    n if n < 0 -> "negative",
    _          -> "big"
  }
}
```

Arms are tested top to bottom. If no arm matches, the match yields `nil`;
exhaustiveness is not statically checked, which is consistent with the
language being dynamically typed. A binding pattern introduces its name into
the arm body's scope only.

### 4.7 Objects

There are no classes. Objects are maps with an optional delegation link to a
prototype. `clone(p)` creates a new map whose prototype is `p`. Property
lookup walks the prototype chain; property assignment always writes to the
receiver.

```rill
let Point = {
  x: 0,
  y: 0,
  norm: fn(self) { sqrt(self.x * self.x + self.y * self.y) }
}

let p = clone(Point)
p.x = 3
p.y = 4
print(p.norm())   # 5
```

Method calls `p.norm()` pass the receiver as the first argument implicitly.
There is no `this` keyword; the receiver is an ordinary named parameter, by
convention `self`.

### 4.8 Builtins

`print`, `clock`, `len`, `str`, `num`, `sqrt`, `floor`, `clone`, `has`, `gc`,
`gcCount`. Builtins are ordinary values bound in the global scope and can be
shadowed.

`keys` was dropped during implementation: Rill has no array type, so it has
nothing meaningful to return. `has(map, name)` covers the actual need, which
is asking whether a name resolves anywhere on a prototype chain. `gc` and
`gcCount` were added so the collector's behaviour is observable from a test
and measurable from a benchmark.

### 4.9 Grammar

```
program     -> exprList EOF
block       -> "{" exprList "}"
exprList    -> ( expr ";"? )*               # value = final expr, else nil
expr        -> assignment
assignment  -> ( call "." )? IDENT "=" assignment | matchExpr
matchExpr   -> "match" expr "{" arm ( "," arm )* ","? "}" | ifExpr
arm         -> pattern ( "if" expr )? "->" expr
pattern     -> literal ( "|" literal )* | IDENT | "_"
ifExpr      -> "if" expr block ( "else" ( ifExpr | block ) )? | logicOr
logicOr     -> logicAnd ( "or" logicAnd )*
logicAnd    -> equality ( "and" equality )*
equality    -> comparison ( ( "==" | "!=" ) comparison )*
comparison  -> term ( ( "<" | ">" | "<=" | ">=" ) term )*
term        -> factor ( ( "+" | "-" ) factor )*
factor      -> unary ( ( "*" | "/" | "%" ) unary )*
unary       -> ( "!" | "-" ) unary | call
call        -> primary ( "(" args? ")" | "." IDENT )*
primary     -> NUMBER | STRING | "true" | "false" | "nil" | IDENT
             | "(" expr ")" | block | fnExpr | mapLit
             | "while" expr block | "let" ... | "var" ... | "break"
             | "continue" | "return" expr?
fnExpr      -> "fn" "(" params? ")" block
mapLit      -> "{" ( IDENT ":" expr ( "," IDENT ":" expr )* ","? )? "}"
```

Ambiguity note: `{` begins both a block and a map literal. The parser resolves
this with two tokens of lookahead past `{`. It is a map literal when the next
token is `}` (the empty case) or when an `IDENT` is followed by `:`; otherwise
it is a block. So `{}` is the empty **map**, and an empty block must be
written `{ nil }`.

## 5. Implementation Architecture

Single-pass means there is no AST: the Pratt parser calls the compiler's emit
functions directly as it parses. The code is still split into focused
translation units rather than one large file.

```
src/
  common.hpp       Build flags, fixed limits, small utilities
  value.hpp/cpp    Value representation, printing, equality
  object.hpp/cpp   Obj header and subtypes; allocation entry points
  table.hpp/cpp    Open-addressing hash table (strings, globals, maps)
  chunk.hpp/cpp    Bytecode array, constant pool, line info
  lexer.hpp/cpp    Source to token stream
  parser.hpp/cpp   Pratt table, precedence climbing
  compiler.hpp/cpp Scopes, locals, upvalues, emission, constant folding
  vm.hpp/cpp       Dispatch loop, call frames, runtime errors
  gc.hpp/cpp       Mark-sweep collector, root enumeration, stress mode
  debug.hpp/cpp    Disassembler, trace output
  builtins.cpp     Native function implementations
  recorder.hpp/cpp Time-travel undo log (milestone 11)
  main.cpp         CLI: file runner, REPL, debugger REPL
```

Boundaries: the lexer knows nothing of the compiler; the parser owns
precedence and delegates all code generation to the compiler; the compiler
knows nothing of the VM's dispatch loop; the GC knows only the `Obj` header
and a root-enumeration callback provided by the VM and compiler.

### 5.1 Value representation

Phase one is a readable tagged union so that bring-up and GC debugging are
tractable:

```cpp
enum class ValueType { Nil, Bool, Number, Obj };
struct Value {
  ValueType type;
  union { bool boolean; double number; Obj* obj; } as;
};
```

Phase two adds NaN-boxing behind `-DRILL_NANBOX=ON`, packing every value into
a `uint64_t`: a quiet-NaN payload tags `nil`, `true`, `false`, and pointers,
while any other bit pattern is the double itself. Both representations sit
behind the same accessor functions, so no other file changes. This is one of
the measured optimizations in section 7.

### 5.2 Object model

Every heap object begins with a common header, and all objects are threaded
onto an intrusive singly linked list owned by the GC.

```cpp
enum class ObjType { String, Function, Closure, Upvalue, Native, Map };
struct Obj { ObjType type; bool isMarked; Obj* next; };
```

`ObjString` stores its length, a cached hash, and its characters.
`ObjFunction` holds a `Chunk`, arity, upvalue count, and name. `ObjClosure`
holds a function pointer and an array of `ObjUpvalue*`. `ObjUpvalue` holds a
`Value*` location plus a `closed` slot. `ObjMap` holds a `Table` and a
prototype pointer.

### 5.3 Compiler

The compiler maintains a stack of `Compiler` records, one per function being
compiled. Each holds the enclosing compiler, the function under construction,
a fixed array of `Local` entries (name, scope depth, `isCaptured`,
`isMutable`), an upvalue array, and the current scope depth.

Local resolution walks the local array backward. Upvalue resolution recurses
into enclosing compilers, adding upvalue entries as it unwinds — the standard
approach, and the reason closures work without an AST.

Jump patching uses two-byte relative offsets with placeholder emission and
backpatching. `break` requires a per-loop list of pending jumps patched at
loop end; `continue` emits a backward jump to the loop's condition.

### 5.4 Bytecode

Roughly 40 opcodes in the base set: constants and literals; `POP`, `DUP`;
local, global, and upvalue access; property get and set; arithmetic,
comparison, and logical negation; jumps and loops; `CALL`, `CLOSURE`,
`CLOSE_UPVALUE`, `RETURN`; and map construction. Operands are one byte, except
jump offsets which are two. The constant pool is limited to 256 entries per
chunk in the base design, with a `CONSTANT_LONG` escape for larger programs.

Line information is stored run-length encoded alongside the code array so
runtime errors can report source lines without a per-byte line array.

### 5.5 VM

```cpp
struct CallFrame { ObjClosure* closure; uint8_t* ip; Value* slots; };
```

A fixed value stack and a fixed frame array, both with overflow checks that
produce runtime errors rather than crashes. The dispatch loop is a `switch`
over the opcode byte, with a computed-goto variant behind
`-DRILL_COMPUTED_GOTO=ON` for GCC and Clang.

Runtime errors unwind by printing a stack trace built from the frame array and
returning an error status to the caller; the REPL recovers, the file runner
exits with status 70.

## 6. Garbage Collector

Mark-and-sweep with precise roots. No conservative stack scanning.

**Roots.** The VM value stack, every call frame's closure, the globals table,
the open-upvalue list, and an explicit temporary-root stack. The temporary
roots matter: a function under construction, or a freshly allocated string not
yet stored anywhere, is unreachable from any other root and will be collected
mid-construction without it. This is the single most common bug in a
from-scratch collector and the design accounts for it explicitly.

**Algorithm.** Mark roots gray onto a worklist; drain the worklist, blackening
each object by tracing its references; then sweep the intrusive object list,
freeing every unmarked object and clearing the mark bit on survivors.

**String interning.** The intern table holds weak references. Unmarked entries
must be removed from the table *after* marking and *before* sweeping, or the
table retains pointers to freed memory.

**Triggering.** A running `bytesAllocated` counter against a `nextGC`
threshold; after each collection `nextGC = bytesAllocated * 2`, with a floor.

**Stress mode.** `-DRILL_GC_STRESS=ON` runs a collection before every
allocation. The full test suite runs in this mode in CI. This converts latent
collector bugs into deterministic, immediate test failures.

**Verification.** The whole test suite runs under
`valgrind --leak-check=full --errors-for-leak-kinds=all --error-exitcode=1`.
The VM frees every remaining object at teardown so that any reported leak is a
real defect rather than expected shutdown noise. A separate CI job runs
AddressSanitizer and UndefinedBehaviorSanitizer.

## 7. Measured Optimizations

Four optimizations, each behind an independent build flag so the benchmark
table is an ablation study rather than a single before/after pair.

1. **Constant folding** (`RILL_FOLD`). Single-pass folding requires the
   compiler to track whether the last two emitted instructions were constant
   loads; when they were and a binary arithmetic operator follows, the three
   instructions are replaced by one constant load of the computed value.
   Folding is skipped when it would change semantics (division by zero).
2. **Specialized opcodes** (`RILL_SUPEROPS`). `GET_LOCAL_0` through
   `GET_LOCAL_3`, `ADD_CONST`, `INC_LOCAL`, and `JUMP_IF_FALSE_POP`, chosen
   after measuring the actual opcode-pair frequencies emitted by the benchmark
   programs rather than guessed.
3. **NaN-boxing** (`RILL_NANBOX`). Halves `Value` from 16 bytes to 8.
4. **Computed-goto dispatch** (`RILL_COMPUTED_GOTO`). Replaces the switch with
   a jump table indexed by opcode, removing a bounds check and improving
   branch prediction.

Each is reported as a percentage change against the same baseline on the same
four benchmarks, run in the same container, with median and minimum of N
repetitions.

## 8. Benchmarks

### 8.1 Programs

Four microbenchmarks chosen to stress distinct subsystems:

| Benchmark | Stresses |
|---|---|
| `fib(30)` recursive | Call and return overhead, frame setup |
| Tight numeric loop | Dispatch cost, arithmetic, local access |
| String building | Allocation churn, interning, GC frequency |
| Binary trees | Object allocation, pointer chasing, GC pressure |

Each is written twice: idiomatic Rill and idiomatic Python, doing the same
work with the same algorithm.

### 8.2 Harness

`bench/run_bench.py` runs each program N times in each VM, discards a warmup
run, and reports median and minimum wall-clock time plus peak RSS. It emits
both a human-readable table and a JSON file. Everything runs inside the
project's Docker image so the numbers are reproducible.

### 8.3 Honesty requirements

The benchmark writeup must state: the exact CPython version; that Rill has
doubles where CPython has arbitrary-precision integers, and which benchmarks
that helps or hurts; that these are microbenchmarks and do not generalize to
whole programs; and the machine and container the numbers came from. Any
benchmark where Rill loses is reported alongside the ones where it wins.

## 9. Testing

### 9.1 Behavior tests

Each test is a `.rl` file carrying its expectations inline:

```rill
# expect: 5
print(2 + 3)
```

A runner executes each file, compares stdout line by line against the
`# expect:` annotations, and checks compile and runtime errors against
`# expect error:` annotations. 100+ tests distributed across: lexing and
literals; operator precedence; blocks as expressions; `let` and `var`
semantics including immutability errors; `if` as an expression; `while`,
`break`, `continue`; `match` patterns, alternatives, guards, and fallthrough;
functions, arity errors, recursion; closures and upvalue capture including
loop-variable capture; prototype objects and delegation; runtime type errors;
and GC behavior under stress mode.

### 9.2 Unit tests

No third-party libraries means no gtest or Catch2. A roughly 30-line assertion
harness in `tests/unit/` covers the hash table, the lexer's token stream, the
chunk encoder, and the collector's mark phase in isolation.

### 9.3 CI

GitHub Actions, all jobs inside the project image:

- Build and test matrix: {gcc, clang} x {Debug, Release}.
- GC stress job: full behavior suite with `RILL_GC_STRESS=ON`.
- Valgrind job: full behavior suite under valgrind, `--error-exitcode=1`.
- Sanitizer job: ASan and UBSan.
- Optimization matrix: each optimization flag toggled, verifying identical
  program output across all configurations.
- A guard that fails the build if the behavior test count drops below 100.

## 10. Time-Travel Debugger

Deferred until milestones 0 through 10 are complete and green.

**Approach.** An undo log rather than periodic full snapshots. Full snapshots
cost O(heap) per step; the log costs O(mutations). Before each mutating
operation the recorder appends the inverse: the previous value of a local,
global, or map field; the value popped from the stack; the frame pushed or
popped. Stepping backward replays the log in reverse.

**Fallback.** If undo-log complexity proves unmanageable — closures and the GC
interacting badly with retained old values are the risk — the alternative is
periodic keyframe snapshots plus deterministic forward replay from the nearest
keyframe. This trades time for memory and is strictly simpler to make correct.

**Interaction with the GC.** The undo log holds values that may be otherwise
unreachable, so the log becomes a GC root while recording is enabled. This is
the design's main new invariant.

**REPL.** `step`, `back`, `continue`, `run to <line>`, `print <expr>`,
`where`, `record on|off`.

**Measurement.** The same four benchmarks run with recording on and off,
reporting the percentage slowdown and bytes of log per thousand instructions.
A recording-off build must show no measurable regression.

## 11. Milestones

| # | Milestone | Done when |
|---|---|---|
| M0 | Docker, CMake, CI skeleton | A trivial binary builds and one test runs in CI |
| M1 | Lexer | Token stream dumped for all token types; lexer unit tests pass |
| M2 | Pratt parser, compiler, VM core | Arithmetic expressions evaluate; disassembler output is correct |
| M3 | Bindings and scopes | `let`/`var`, globals, locals, blocks as expressions; immutability is a compile error |
| M4 | Control flow | `if` as expression, `while`, `break`, `continue`, `and`/`or` |
| M5 | Functions and closures | Calls, recursion, upvalue capture, arity errors |
| M6 | Match | Literals, alternatives, guards, wildcard, binding patterns |
| M7 | Garbage collector | Stress mode green, valgrind clean on the full suite |
| M8 | Prototype objects | Map literals, property access, `clone`, delegation, method receiver |
| M9 | Optimizations and benchmarks | Four flags, ablation table, CPython comparison published |
| M10 | Test suite and CI hardening | 100+ behavior tests, full CI matrix green |
| M11 | Time-travel debugger | Backward stepping in the REPL with measured overhead |

## 12. Risks

| Risk | Mitigation |
|---|---|
| GC bugs surfacing late and being hard to localize | Stress mode from the moment the collector exists; valgrind in CI from M7 |
| Compiler-allocated objects collected mid-construction | Explicit temporary-root stack, designed in from the start (section 6) |
| `{` ambiguity between block and map literal | One-token lookahead; `{}` defined as an empty map (section 4.9) |
| Benchmarks that flatter Rill unfairly | Disclosure requirements in section 8.3; report losses alongside wins |
| Time-travel debugger scope overrunning | Strictly last; keyframe-plus-replay fallback defined in section 10 |
| Single-pass constraint blocking an optimization | Constant folding designed against a peephole window, not a tree walk (section 7) |
