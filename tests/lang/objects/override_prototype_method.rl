# expect: 3
let A = { greet: fn(self) { 1 } }
let B = clone(A)
B.greet = fn(self) { 3 }
print(B.greet())
