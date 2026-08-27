# String building: allocation churn, interning, collection frequency.
var i = 0
var n = 0
while i < 60000 {
  let s = "item-" + str(i) + "-end";
  n = n + len(s);
  i = i + 1;
}
print(n)
