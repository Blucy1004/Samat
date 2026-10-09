fn tokens(text: String) -> List<String>:
    let result: List<String> = []
    for token in text.split(" "):
        if token.length > 0:
            result.push(token)
    return result
fn main() -> Int:
    let words = tokens("let x = 10")
    println(words)
    if words.length == 4 and words[0] == "let" and words[3] == "10":
        return 42
    return 1
