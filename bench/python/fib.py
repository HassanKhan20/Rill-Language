# Recursive fib: call and return overhead, frame setup.
def fib(n):
    return n if n < 2 else fib(n - 1) + fib(n - 2)
print(fib(27))
