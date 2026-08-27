# Recursive fib: call and return overhead, frame setup.
let fib = fn(n) { if n < 2 { n } else { fib(n - 1) + fib(n - 2) } }
print(fib(27))
