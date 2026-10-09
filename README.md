# Samat

> 사람의 뜻이 CPU에 사맛게 하노라.

Samat은 읽기 쉬운 표현과 빠른 실행을 함께 목표로 하는 프로그래밍 언어입니다.

Samat Code Syntax(`.st`)와 訓C正音은 같은 파서, AST, 타입 검사기, 실행 체계를 공유합니다. 해례(`.hy`)는 장면을 기술하는 선언형 형식이며, Samat과 함께 독립적으로 파싱하고 검증할 수 있습니다.

## 빠르게 시작하기

저장소 루트의 [main.st](main.st)는 Samat 소개와 대화형 계산기를 담은 실행 예제입니다. 실행하면 첫 번째 숫자, 연산자, 두 번째 숫자를 순서대로 입력합니다.

```text
7
*
7
```

## 빌드

필요한 도구는 CMake 3.24 이상과 C++20 컴파일러입니다. LLVM은 선택 사항이며, 켜려면 LLVM 17 이상이 설치되어 있어야 합니다.

### Windows PowerShell

```powershell
cmake -S . -B build -A x64 -DSAMAT_ENABLE_LLVM=OFF
cmake --build build --config Release --parallel 2
.\build\Release\Samat.exe check .\main.st
.\build\Release\Samat.exe run .\main.st
```

### Linux 또는 macOS

```sh
cmake -S . -B build -DSAMAT_ENABLE_LLVM=OFF
cmake --build build --parallel 2
./build/Samat check main.st
./build/Samat run main.st
```

`check`는 프로그램을 실행하지 않고 구문과 타입을 검사합니다. `run`은 인터프리터로 실행합니다. 사용 가능한 명령은 `Samat --help`에서 확인할 수 있습니다.

## 언어 예제

```samat
fn factorial(n: Int) -> Int:
    if n <= 1:
        return 1
    return n * factorial(n - 1)

fn main() -> Int:
    let values: List<Int> = [1_000, 0x2A, 0b1010]
    if values.isEmpty():
        return 0
    return factorial(5) + values[0]
```

Samat v1.0에는 다음 기능이 포함됩니다.

- `Int`, `Float`, `Bool`, `String`, `List`, `Map`, `Tuple`, `Struct`, `Optional` 타입
- 함수, 이름 있는 인자, 조건문, 반복문, 범위, 모듈과 가져오기
- 문자열과 컬렉션 메서드. 문자열과 리스트의 `.isEmpty()` 포함
- `1_000_000`, `0xFF`, `0b1010` 형태의 정수 표기
- 콘솔 입출력, 수학, 난수, 시간 기능
- 기본 인터프리터, 선택형 LLVM JIT/AOT 백엔드, 지원 범위 내의 x64 네이티브 백엔드

인터프리터와 네이티브 백엔드가 지원하는 기능 범위는 서로 다릅니다. 자세한 내용은 [네이티브 컴파일 안내](docs/Samat-Native-Compilation.md)를 참고하세요.

## 해례

해례 파일은 장면 이름, 배경색, 도형, 위치, 크기, 회전, 색상을 선언합니다. 현재 저장소에는 해례 파서와 검증기, 직렬화기, 예제 장면이 포함되어 있습니다.

- [해례 형식 안내](docs/Haerye-v0.1.md)
- [예제 장면](examples/Haerye/pong.hy)

## 테스트

```sh
ctest --test-dir build -C Release --output-on-failure
```

테스트는 Code Syntax와 訓C正音의 AST 일치, 파싱과 타입 검사, 인터프리터, 네이티브 백엔드, 런타임 메모리, `main.st`의 줄바꿈별 입력 동작, 해례 파싱·검증·직렬화를 확인합니다. LLVM을 사용할 수 없는 환경에서는 LLVM 전용 검사가 실행 불가로 표시됩니다.

## 저장소 구성

- `src/Samat/Language/` — 파서, AST, 타입 검사기, 인터프리터, 네이티브 백엔드
- `src/Samat/` — 해례 파서와 호스트 앱용 언어 도구 지원
- `src/Samat/CLI/` — `Samat` 명령줄 실행기
- `examples/Samat/v1.0/` — Samat 언어 예제
- `examples/Samat/archive/` — 이전 버전 예제
- `examples/Haerye/` — 해례 장면 예제
- `docs/` — 언어, 백엔드, 해례 문서

이전 버전 자료는 기록을 위해 보관합니다. 현재 Samat 소스 확장자는 `.st`입니다.
