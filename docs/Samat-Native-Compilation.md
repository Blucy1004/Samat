# Samat 네이티브 컴파일

Samat은 인터프리터를 기본 실행 경로로 사용합니다. 선택적으로 LLVM을 통한 JIT/AOT 기능과, 제한된 정수 연산 부분집합을 위한 x64 네이티브 백엔드를 사용할 수 있습니다. Code Syntax와 訓C正音 모두 같은 AST와 중간 표현을 사용합니다.

## 실행 흐름

```text
Code Syntax ─┐
             ├─> shared Samat AST ─> type checker ─> interpreter
訓C正音 ─────┘                           └──────────> JM IR ─> native backend
```

JM IR은 함수, 매개변수, 지역 변수, 임시 값, 기본 블록, 정수 연산, 호출, 조건 분기, 반복문, 반환을 표현합니다. 예를 들어 다음 함수는

```samat
fn add(a: Int, b: Int) -> Int:
    return a + b
```

대략 다음과 같은 중간 표현으로 변환됩니다.

```text
func add(2 x i64) -> i64 {
entry:
  %0 = load.local 0
  %1 = load.local 1
  %2 = op.add.i64 %0, %1
  ret %2
}
```

## 사용 가능한 명령

```powershell
Samat.exe check main.st
Samat.exe run main.st
Samat.exe --ir examples/Samat/archive/native-factorial.st
Samat.exe --native main examples/Samat/archive/native-factorial.st
```

`check`는 파싱과 타입 검사를 수행하고, `run`은 인터프리터로 실행합니다. `--ir`는 Samat IR을 출력하며 `--native`는 사용 가능한 네이티브 백엔드로 실행합니다. 명령줄 인자는 `Samat --help`에 설명되어 있습니다.

## x64 부트스트랩 백엔드 범위

현재 x64 부트스트랩 백엔드는 부호 있는 64비트 정수와 불리언 리터럴, 변경 가능한 지역 변수, 단항 `-`와 `!`, 산술·비교·논리 연산, `if/else`, `while`, 직접 함수 호출, 매개변수, 반환, 재귀를 지원합니다. 한국어 함수와 반환 표현도 같은 중간 표현으로 컴파일됩니다.

리스트, 맵, 부동소수점, 문자열, 클로저, 중첩 선언, `for`, `break`, `continue`, 전역 변수는 이 백엔드에서 아직 지원하지 않습니다. 이런 프로그램은 인터프리터로 실행할 수 있습니다. LLVM 기능은 LLVM 17 이상을 설치한 뒤 CMake에서 `-DSAMAT_ENABLE_LLVM=ON`으로 설정해 빌드합니다.
