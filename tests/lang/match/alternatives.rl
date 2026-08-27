# expect: small
# expect: small
let d = fn(v) { match v { 1 | 2 -> "small", _ -> "big" } }
print(d(1))
print(d(2))
