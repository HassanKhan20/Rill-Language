# expect: 25
print(match 5 { n -> match n { 5 -> n * n, _ -> 0 }, _ -> -1 })
