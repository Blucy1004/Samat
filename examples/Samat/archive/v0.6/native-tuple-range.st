fn main() -> Int:
    let data = (30, 1.5, "JM")
    let total = 0
    for value in range(6, 0, -2):
        total += value
    return data.0 + total
