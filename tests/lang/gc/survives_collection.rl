# A closure reachable from a global must survive a collection.
# expect: 3
let counter = fn() { var n = 0; fn() { n = n + 1; n } }
let c = counter()
c()
c()
gc()
print(c())
