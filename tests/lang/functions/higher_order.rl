# expect: 7
let apply = fn(f, v) { f(v) }
print(apply(fn(x) { x + 2 }, 5))
