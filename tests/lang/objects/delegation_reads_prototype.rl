# expect: 7
let proto = { v: 7 }
let child = clone(proto)
print(child.v)
