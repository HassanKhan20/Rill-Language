# expect: 4
let f = fn(n) { 1 + match n { 1 -> 3, _ -> 0 } }
print(f(1))
