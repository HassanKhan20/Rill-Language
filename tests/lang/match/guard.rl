# expect: negative
let d = fn(v) { match v { 0 -> "zero", n if n < 0 -> "negative", _ -> "positive" } }
print(d(-4))
