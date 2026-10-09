fn main() -> Int:
    let values: List = [5, 3, 8, 1, 2]
    for i in 0..values.length:
        for j in 0..values.length - i - 1:
            if values[j] > values[j + 1]:
                let temporary = values[j]
                values[j] = values[j + 1]
                values[j + 1] = temporary
    assert(values == [1, 2, 3, 5, 8])
    print(values)
    return values[0]
