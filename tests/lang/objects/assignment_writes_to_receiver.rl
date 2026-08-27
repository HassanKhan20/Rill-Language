# expect: 1
# expect: 7
let proto = { v: 7 }
let child = clone(proto)
child.v = 1
print(child.v)
print(proto.v)
