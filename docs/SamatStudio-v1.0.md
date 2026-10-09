# Samat v1.0 / Samat Studio

**사람의 뜻이 CPU에 사맛게 하노라.**

Samat Code (`.st`) and 訓C正音 use the same parser, shared AST, type checker, interpreter, and native backend. v1.0 adds readable numeric literals (`1_000_000`, `0xFF`, `0b1010`) and `.isEmpty()` for strings and lists. Existing source forms remain supported. These additions do not introduce a separate parser, AST, or backend.

## Build and verify on Windows

Requirements: Windows 10/11 x64, Visual Studio C++ workload and Windows SDK, CMake 3.24+, Git, and network access for the first dependency fetch. LLVM is optional and disabled in the portable package.

```powershell
cmake -S . -B build -A x64 -DJMENGINE_BUILD_ENGINE=ON -DJMENGINE_BUILD_SANDBOX=ON -DJMENGINE_ENABLE_LLVM=OFF
cmake --build build --config Release --parallel 2
ctest --test-dir build -C Release --output-on-failure
cmake --build build --target package --config Release
```

The portable ZIP includes Samat Studio, the retained legacy sandbox alias, the `Samat.exe` command-line runner, runtime library, examples, and the editor license. The standalone `--version` output identifies Samat 1.0.0. The Studio and CLI use static SDL/MSVC runtimes; only Windows inbox DLLs are imported.

## Language changes in v1.0

- Digit separators are allowed only between digits: `1_000_000`.
- Binary and hexadecimal integer literals are supported: `0b1010`, `0xFF`.
- `.isEmpty()` returns Bool for String and List values.
- Both `.st` and 訓C正音 continue through the existing shared language pipeline.

## Studio scope and limits

The Studio provides a Samat source editor and interpreter plus a visual Haerye scene workspace and game preview. Scene property changes are preview-only and are not saved back into `.hy`. The interpreter runs in-process; it has instruction, recursion, and printed-output limits, but a native crash can still terminate the Studio. Completion is intentionally lightweight, and diagnostic underlines are not implemented.

The automated regression suite and Windows package checks are the release verification record. The interactive Studio workflows still need a brief hands-on check on the target release machine: open/save a `.st`, run a sample, inspect a syntax error, and start/stop the Pong preview. Passing automated checks alone does not verify those manual interactions.
