# expect: 120
let fact = fn(n) { if n <= 1 { 1 } else { n * fact(n - 1) } }
print(fact(5))
