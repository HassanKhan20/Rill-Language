# expect error: stack overflow
let f = fn() { f() }
f()
