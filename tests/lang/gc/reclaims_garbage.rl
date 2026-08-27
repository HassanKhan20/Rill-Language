# The collector must reclaim short-lived strings rather than growing
# without bound. 5000 dead strings is far more than the live set.
# expect: true
var i = 0
while i < 5000 { let s = "garbage" + str(i); i = i + 1; }
print(gc() < 200000)
