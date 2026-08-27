# expect: true
# 0 and the empty string are truthy: only nil and false are falsey.
print(if 0 { true } else { false })
