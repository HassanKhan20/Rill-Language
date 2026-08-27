# expect: hit
print(match "b" { "a" -> "no", "b" -> "hit", _ -> "other" })
