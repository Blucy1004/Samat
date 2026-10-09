import jm.math
let moveSpeed: Float = 8.0
let globalScore: Int = 0

fn factorial(n: Int) -> Int:
    if n <= 1:
        return 1
    return n * factorial(n - 1)

fn main() -> Int:
    globalScore += 1
    if factorial(10) == 3628800 and globalScore == 1:
        return 42
    return 1
