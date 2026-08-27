# expect: big
let d = fn(v) { match v { 1 | 2 -> "small", _ -> "big" } }
print(d(9))
