# expect: true
# expect: false
let proto = { v: 1 }
let child = clone(proto)
print(has(child, "v"))
print(has(child, "nope"))
