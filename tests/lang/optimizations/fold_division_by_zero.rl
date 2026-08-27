# Folding must not change semantics: division by zero yields the same
# infinity whether computed at compile time or run time.
# expect: inf
print(1 / 0)
