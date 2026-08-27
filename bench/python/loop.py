# Tight numeric loop: dispatch cost, arithmetic, local access.
i = 0
total = 0
while i < 3000000:
    total = total + i * 2 - 1
    i = i + 1
print(total)
