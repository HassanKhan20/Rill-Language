# Binary trees: object allocation, pointer chasing, GC pressure.
def make(depth):
    if depth == 0:
        return {"leaf": True}
    return {"leaf": False, "l": make(depth - 1), "r": make(depth - 1)}

def count(node):
    if node["leaf"]:
        return 1
    return 1 + count(node["l"]) + count(node["r"])

total = 0
for _ in range(24):
    total += count(make(12))
print(total)
