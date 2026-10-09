import console

print("================================")
print("              Samat")
print("================================")
print("")
print("사람의 뜻이 CPU에 사맛게 하노라.")
print("")
print("Samat은 독립형 프로그래밍 언어입니다.")
print("쉬운 표현과 강력한 실행 성능을 함께 목표로 하며,")
print("Code Syntax와 訓C正音이 하나의 AST와 실행 체계를 공유합니다.")
print("")
print("Samat v1.0")
print("Safe & Live")
print("")
print("--------------------------------")
print("Samat Calculator")
print("--------------------------------")
print("")

fn calculate(first: Float, operator: String, second: Float) -> Float:
    if operator == "+":
        return first + second
    if operator == "-":
        return first - second
    if operator == "*":
        return first * second
    if operator == "/":
        if second == 0.0:
            assert(second != 0.0)
        return first / second
    print("지원하지 않는 연산자입니다.")
    return 0.0

print("첫 번째 숫자:")
let first = float(readLine())
print("연산자 (+ - * /):")
let operator = readLine()
print("두 번째 숫자:")
let second = float(readLine())
let result = calculate(first, operator, second)
print("")
print("결과: " + string(result))
print("")
print("THE main.st IS REAL!!!")
print("================================")
