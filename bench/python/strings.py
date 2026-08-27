# String building: allocation churn, interning, collection frequency.
i = 0
n = 0
while i < 60000:
    s = "item-" + str(i) + "-end"
    n = n + len(s)
    i = i + 1
print(n)
