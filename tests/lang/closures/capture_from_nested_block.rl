# expect: 5
let f = fn() { let a = 5; { let b = 1; fn() { a } } }
print(f()())
