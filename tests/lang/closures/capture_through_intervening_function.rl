# expect: 10
let outer = fn() {
  let x = 10;
  let mid = fn() { fn() { x } };
  mid()()
}
print(outer())
