# Binary trees: object allocation, pointer chasing, GC pressure.
let make = fn(depth) {
  if depth == 0 {
    { leaf: true }
  } else {
    { leaf: false, l: make(depth - 1), r: make(depth - 1) }
  }
}
let count = fn(node) {
  if node.leaf { 1 } else { 1 + count(node.l) + count(node.r) }
}
var total = 0
var i = 0
while i < 24 {
  total = total + count(make(12));
  i = i + 1;
}
print(total)
