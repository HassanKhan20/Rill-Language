# expect: 7
# expect: 12
let makeAdder = fn(n) { fn(x) { x + n } }
let add3 = makeAdder(3)
let add10 = makeAdder(10)
print(add3(4))
print(add10(2))
