# Constant-heavy arithmetic: the workload constant folding is FOR. The other
# benchmarks deliberately contain no foldable subexpressions, so folding does
# nothing for them; this one shows what it is worth when it applies.
var i = 0
var total = 0
while i < 1500000 {
  total = total + (2 * 3 + 4) * (10 - 7) + (100 / 4) - (8 % 3);
  i = i + 1;
}
print(total)
