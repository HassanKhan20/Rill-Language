# expect: true
let before = gcCount()
var i = 0
while i < 3000 { let s = str(i) + "x"; i = i + 1; }
gc()
print(gcCount() > before)
