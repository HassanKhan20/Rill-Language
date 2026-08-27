# expect: 7
let v = 3
print(match v { 3 -> { let a = 4; a + 3 }, _ -> 0 })
