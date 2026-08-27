# expect error: already a binding with this name in this scope
print({ let x = 1; let x = 2; x })
