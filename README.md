# Samat

> 사람의 뜻이 CPU에 사맛게 하노라.

Samat is a programming language developed for JM Engine. It aims to pair approachable syntax with fast execution.

**Samat Code Syntax (`.st`) and 訓C正音 share one parser, AST, type checker, and runtime.** Haerye (`.hy`) is the companion format for describing scenes. The language and the engine are separate parts of the same toolchain.

## Try Samat

The repository includes [a short Samat introduction and interactive calculator](main.st). Run it and enter one value per line:

```text
7
*
7
```

### Build the command-line runner

Requirements: CMake 3.24+, a C++20 compiler, and Git. The engine build fetches SDL3, Dear ImGui, and nlohmann/json on first configure. LLVM is optional.

Windows PowerShell:

```powershell
cmake -S . -B build -A x64 -DJMENGINE_ENABLE_LLVM=OFF
cmake --build build --config Release --parallel 2
.\build\Release\Samat.exe check .\main.st
.\build\Release\Samat.exe run .\main.st
```

Linux or macOS:

```sh
cmake -S . -B build -DJMENGINE_ENABLE_LLVM=OFF
cmake --build build --parallel 2
./build/Samat check main.st
./build/Samat run main.st
```

`run` starts the interpreter. For the calculator, provide the first number, operator, and second number on separate input lines. `check` parses and type-checks a program without running it. Use `Samat --help` to see the compiler, interpreter, and optional native commands.

### Build Samat Studio

Samat Studio is a Windows x64 application for editing `.st` code, opening `.hy` scenes, and running Samat against a scene in Game Preview.

```powershell
cmake -S . -B build -A x64 -DJMENGINE_BUILD_ENGINE=ON -DJMENGINE_BUILD_SANDBOX=ON -DJMENGINE_ENABLE_LLVM=OFF
cmake --build build --config Release --parallel 2
ctest --test-dir build -C Release --output-on-failure
cmake --build build --target package --config Release
.\build\Release\SamatStudio.exe
```

The portable archive is written to `build/SamatStudio-1.0.0-Windows-x64.zip`. Studio scene-property edits are currently previews and do not save back into the `.hy` file.

## Language at a glance

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

The v1.0 language includes:

- `Int`, `Float`, `Bool`, `String`, `List`, `Map`, `Tuple`, `Struct`, and `Optional` values
- Functions, named arguments, conditionals, loops, ranges, imports, and modules
- String and collection methods, including `.isEmpty()` on strings and lists
- Readable integer literals such as `1_000_000`, `0xFF`, and `0b1010`
- Console input/output, math, random, time, and JM Engine APIs when hosted by the engine
- An interpreter by default, an optional LLVM JIT/AOT backend, and a bootstrap x64 native backend for its supported subset

The interpreted language and the optional native backends have different capability ranges. See [Samat Native Compilation](docs/Samat-Native-Compilation.md) before relying on a native-only feature.

## Samat Studio and Haerye

Samat source controls behavior. Haerye scene files describe visual objects. Open a `.hy` scene and a `.st` source in Studio, then use Play to run the source in the scene preview. The standalone runner also works for programs that do not use a scene.

The included [Pong scene and Samat program](examples/Pong/) demonstrate the pair. The [Haerye v0.1 notes](docs/Haerye-v0.1.md) describe its current supported scene format.

## Verification

Run the complete automated suite with:

```sh
ctest --test-dir build -C Release --output-on-failure
```

The suite covers Studio and engine smoke runs, LF/CRLF calculator input, Pong gameplay, Haerye parsing, shared Code/Korean AST round-trips, type-checking, interpreter behavior, runtime memory, and available native backends. LLVM-dependent cases are reported as unavailable when LLVM is disabled or absent.

## Project map

- `src/JMEngine/Script/` — Samat language, shared AST, type checker, interpreter, and native backends
- `src/JMEngine/Core/` — Studio and engine integration
- `src/JMEngine/Scene/` — Haerye and scene model
- `src/ScriptCli/` — `Samat` command-line runner
- `examples/Samat/v1.0/` — current language examples
- `examples/Samat/archive/` — earlier milestone examples
- `examples/Pong/` — paired `.hy` scene and `.st` behavior
- `docs/` — language, backend, Studio, and scene-format documentation

Earlier milestones are kept as historical records; the current source extension is `.st` and the current command-line program is `Samat`.
