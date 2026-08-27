# expect: 1
# expect: 2
let makeCounter = fn() {
  var count = 0;
  fn() { count = count + 1; count }
}
let c = makeCounter()
print(c())
print(c())
