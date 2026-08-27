# expect: 10
let clamp = fn(x, lo, hi) {
  if x < lo { return lo; }
  if x > hi { return hi; }
  x
}
print(clamp(99, 0, 10))
