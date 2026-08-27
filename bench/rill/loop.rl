# Tight numeric loop: dispatch cost, arithmetic, local access.
var i = 0
var total = 0
while i < 3000000 {
  total = total + i * 2 - 1;
  i = i + 1;
}
print(total)
