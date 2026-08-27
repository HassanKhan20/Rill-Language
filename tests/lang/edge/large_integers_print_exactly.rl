# expect: 8999994000000
# Large integral values must print exactly, not in %g scientific form.
var i = 0
var t = 0
while i < 3000000 { t = t + i * 2 - 1; i = i + 1; }
print(t)
