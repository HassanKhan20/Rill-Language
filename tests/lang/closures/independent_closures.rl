# expect: 1
# expect: 1
let makeCounter = fn() { var n = 0; fn() { n = n + 1; n } }
let a = makeCounter()
let b = makeCounter()
print(a())
print(b())
