# Samat

**사람의 뜻이 CPU에 사맛게 하노라.**

한글날 100주년을 맞아 2026년 10월 9일, 오시(午時)에 반포하는 사맛은, 옛날 훈민정음의 창제 정신을 580년이 지난 지금, 컴퓨팅의 영역으로 이어가는 언어입니다.

옛날, 어려운 한문을 배우기 어려운 백성들을 생각하여 만들어진 훈민정음처럼, 사맛과 사맛의 한국어 언어 문법인 훈C정음(訓C正音, CPU를 가르치는 바른 소리)도
코딩을 시작하고 싶은 모든 사람들을 돕는 언어가 되었으면 합니다.

Code Syntax와 訓C正音은 하나의 AST와 실행 체계를 공유합니다.

## 주요 특징

- `.st` 확장자로 작성하는 Samat 소스 코드
- 인터프리터와 네이티브 컴파일 백엔드
- 같은 AST와 런타임을 사용하는 Code Syntax와 訓C正音(한국어 언어 문법)
- 해례(Haerye) 문법 파일의 파싱, 검증, 직렬화
- Windows와 Linux 빌드 지원

## 요구 사항

- CMake 3.24 이상
- C++20 지원 컴파일러
- Windows: Visual Studio 2022 또는 호환 MSVC
- Linux: GCC 또는 Clang

## 빌드

### Windows

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

실행 파일은 다음 위치에 생성됩니다.

```text
build\Release\Samat.exe
```

### Linux

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

실행 파일은 `build/Samat`에 생성됩니다.

## 실행

Samat 소스 파일을 실행합니다.

```powershell
.\build\Release\Samat.exe main.st
```

Linux에서는 다음과 같이 실행합니다.

```bash
./build/Samat main.st
```

명령줄 도움말은 다음 명령으로 확인할 수 있습니다.

```text
Samat --help
```

## 예제

저장소의 `examples/Samat/v1.0` 폴더에서 언어 예제를 확인할 수 있습니다. 가장 간단한 예제는 `hello.st`입니다.

```powershell
.\build\Release\Samat.exe examples\Samat\v1.0\hello.st
```

프로젝트 루트의 `main.st`에는 Samat 소개와 대화형 계산기 예제가 있습니다.

## 訓C正音 해례

訓C正音은 Samat과 실행 체계를 공유하는 한국어 문법 표기입니다. 해례 파일은 `.hy` 확장자를 사용합니다.

예제 파일:

```text
examples/Haerye/pong.hy
```

해례 파일은 CLI의 `haerye` 명령으로 검사하거나 형식 변환할 수 있습니다.

```text
Samat haerye validate examples/Haerye/pong.hy
Samat haerye format examples/Haerye/pong.hy
```

## 테스트

```powershell
ctest --test-dir build -C Release --output-on-failure
```

Linux에서는 빌드 디렉터리에서 다음과 같이 실행합니다.

```bash
ctest --test-dir build --output-on-failure
```

## 라이선스

라이선스 정보는 저장소의 `LICENSE` 파일을 확인하세요.

### 세종대왕님, 정의공주님, 문종대왕님, 집현전의 모든 학자들, 그리고 한글의 창제와 발전에 도움을 주신 모든 분들께 특별한 감사를 전합니다.

### Special thanks to King Sejong, Princess Jeongui, King Munjong, all scholars from Jiphyeonjeon, and all those who contributed to Hangeul.
