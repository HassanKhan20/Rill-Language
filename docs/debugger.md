# Time-travel debugger

Rill records execution so a program can be stepped **backward** in a REPL.

```bash
docker compose run --rm dev ./build/rill --debug program.rl
```

```
rill time-travel debugger — program.rl
Recording is on; every instruction is reversible. Type 'help' for commands.

[line 1] step 0
(rill-dbg) step 100
[end of program] 13 steps recorded
(rill-dbg) print counter
counter = 3
(rill-dbg) back 6
[line 3] step 7
(rill-dbg) print counter
counter = 1
(rill-dbg) back 6
[line 1] step 1
(rill-dbg) print counter
no global named 'counter' at this point
```

That last line is the point: stepping back far enough undoes the global's
*definition*, not merely its most recent value.

## Commands

| Command | Effect |
|---|---|
| `step [n]` | Execute n instructions forward (default 1) |
| `back [n]` | Undo n instructions |
| `line` | Run forward until the source line changes |
| `rewind` | Return to the start of the recording |
| `where` | Print the call stack |
| `print <name>` | Show a global's value *at the current point in time* |
| `stats` | Steps recorded and log size |

## How it works

An **undo log**, not periodic snapshots. A full heap snapshot per step costs
O(heap); an undo log costs O(mutations), which for a stack machine is a small
constant — an instruction touches at most a few stack slots plus at most one
table entry.

Before each instruction the VM records:

- the `ip` and source line it is about to execute at,
- the stack height and frame count,
- the **top three stack values**, which is enough because no Rill opcode
  consumes more than three stack operands,
- the previous value of any deep location the instruction writes — a stack
  slot (`SetLocal`, `SetUpvalue`) or a table entry (`SetGlobal`,
  `DefineGlobal`, `SetProperty`), including whether the key existed at all.

Stepping back pops the newest record, undoes the deep write first (the stack
restore may overwrite the very slot the record refers to), then restores the
frame count, stack height, saved stack values, and `ip`.

**Call frames come back for free.** Frames live in a fixed array and are never
cleared, so restoring `frameCount_` brings a popped frame's contents back
intact; only its `ip` needs rewriting.

**The undo log is a GC root.** It holds values that may be reachable from
nowhere else — a string overwritten by the next instruction is garbage by
every other measure, yet stepping back must produce it again. `markVMRoots`
walks the log. This is the design's one genuinely new invariant, and
`tests/test_debugger.py` covers it with a case that forces collections during
recording and then steps back through them.

## Measured overhead

Release build, minimum of 3 runs, same container as the other benchmarks:

| Benchmark | Normal | Recording | Slowdown | Steps | Log size |
|---|---:|---:|---:|---:|---:|
| `fib` | 0.033s | 1.270s | **38×** | 7.9M | 1.1 GB |
| `trees` | 0.058s | 1.399s | **24×** | 5.3M | 1.1 GB |
| `constfold` | 0.130s | 16.426s | **127×** | 36.0M | 9.1 GB |

At **136 bytes per instruction**. A recording-off build shows no measurable
regression: the recorder is behind a single predictable branch at the top of
the dispatch loop, and the deep-write hooks return immediately when recording
is off.

### This overhead is not acceptable, and here is why

Two separate problems, only one of which is inherent:

1. **The log is unbounded.** Nothing ever trims it, so memory grows linearly
   with instructions executed — 9 GB for a 36M-instruction program. Any
   program that runs for more than a few seconds will exhaust memory. This is
   the disqualifying problem.
2. **136 bytes per step is far more than necessary.** The record is a fixed
   struct carrying three `Value` slots and a full table-write undo whether or
   not the instruction used them. The overwhelming majority of instructions
   need neither.

The fix for both is the one the design document named as the fallback
(`docs/superpowers/specs/…-design.md` §10): **periodic keyframes plus
deterministic replay.** Snapshot the whole VM state every N thousand
instructions, keep undo records only since the last keyframe, and implement
`back` as "restore the nearest keyframe, then replay forward to the target."
That bounds memory by the keyframe interval instead of by program length, and
lets the per-step record shrink to a variable-length encoding.

The current implementation is therefore best understood as **correct but only
practical for short programs** — which is genuinely useful for debugging, the
actual use case, but is not what a production recorder would do. It is
reported here rather than presented as finished work.

## Testing

`tests/test_debugger.py` drives the REPL and checks that backward stepping
restores overwritten globals, undoes definitions entirely, re-enters callee
frames, replays loops deterministically after a rewind, and survives
collections. It runs in CI as its own CTest target.
