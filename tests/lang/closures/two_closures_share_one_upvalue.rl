# expect: 1
# expect: 2
let make = fn() {
  var v = 0;
  let bump = fn() { v = v + 1; nil };
  fn() { bump(); v }
}
let f = make()
print(f())
print(f())
