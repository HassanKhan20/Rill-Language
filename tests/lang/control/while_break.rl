# expect: 0
# expect: 1
var i = 0
while true {
  if i >= 2 { break; }
  print(i);
  i = i + 1;
}
