# AddConst must not fuse a string constant, which it cannot add.
# expect: x1
let f = fn(v) { v + "1" }
print(f("x"))
