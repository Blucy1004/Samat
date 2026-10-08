fn factorial(n: Int) -> Int:
    if n <= 1:
        return 1
    return n * factorial(n - 1)

print("5! =", factorial(5))
