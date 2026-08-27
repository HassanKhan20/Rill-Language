# expect: 6
let f = fn(a) { fn(b) { fn(c) { a + b + c } } }
print(f(1)(2)(3))
