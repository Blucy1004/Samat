fn greet(name: String) -> String:
    return "Hello " + name
fn main() -> Int:
    let text = greet("사람")
    let parts = text.split(" ")
    let values = [5, 1, 8, 2, 3]
    values.sort()
    values[1] = 10
    values.push(4)
    let total = 0
    for value in values:
        total += value
    if text.contains("사람") && parts[1].codepointLength() == 2:
        return total + 11
    return 1
