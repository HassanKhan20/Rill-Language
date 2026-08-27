# expect: 42
let capture = fn() { let secret = 42; fn() { secret } }
let f = capture()
print(f())
