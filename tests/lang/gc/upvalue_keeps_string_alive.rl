# Strings held only by a live upvalue must not be collected.
# expect: hello world
let hold = fn() { let s = "hello" + " " + "world"; fn() { s } }
let f = hold()
gc()
print(f())
