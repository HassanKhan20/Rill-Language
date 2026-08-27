# expect: 2
# expect: 1
let x = 1
print({ let x = 2; x })
print(x)
