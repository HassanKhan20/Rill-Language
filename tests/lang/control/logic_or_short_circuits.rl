# expect: true
# a falsey left operand is required for the right side to be evaluated at all,
# so `undefined_thing` never runs
print(true or undefined_thing)
