# Constant-heavy arithmetic. CPython folds these in its own peephole pass, so
# this benchmark is not a fair Rill-vs-CPython comparison; it exists for the
# Rill-vs-Rill ablation.
i = 0
total = 0
while i < 1500000:
    total = total + (2 * 3 + 4) * (10 - 7) + (100 // 4) - (8 % 3)
    i = i + 1
print(total)
