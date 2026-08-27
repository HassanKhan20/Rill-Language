# expect: 12
let o = { f: fn(self, k) { self.base * k } , base: 3 }
print(o.f(4))
