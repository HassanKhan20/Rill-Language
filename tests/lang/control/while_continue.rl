# expect: 1
# expect: 3
var i = 0
while i < 4 {
  i = i + 1;
  if i % 2 == 0 { continue; }
  print(i);
}
