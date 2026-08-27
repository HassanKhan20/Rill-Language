# expect: 5
let Point = {
  x: 0,
  y: 0,
  norm: fn(self) { sqrt(self.x * self.x + self.y * self.y) }
}
let p = clone(Point)
p.x = 3
p.y = 4
print(p.norm())
