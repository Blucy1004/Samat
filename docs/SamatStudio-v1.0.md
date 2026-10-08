# Samat v1.0 / Samat Studio — implementation status

This is a partial Studio milestone on top of the Samat 0.8 language core. It does not claim v1.0 release readiness. The editor reuses the existing Samat parser, checker, interpreter, engine metadata, and Code/訓C正音 AST.

## Use the Studio

Build and launch from the repository root so the example paths resolve:

```powershell
cmake -S . -B build -A x64 -DJMENGINE_ENABLE_LLVM=OFF
cmake --build build --config Release --parallel 2
./build/Release/SamatStudio.exe
```

The older `jmengine_sandbox.exe` filename is also copied beside `SamatStudio.exe` for existing scripts. In the **Samat Studio** tab, enter a `.st` path, then use Open, Save, or Save As. The sample menu includes Hello World, a calculator, loops, a function example, a Korean function example, and the existing Pong scene script. Samples open as unsaved copies so editing them cannot overwrite the repository examples. Open accepts invalid source so it can be corrected; Save writes the text as entered, even when it has syntax errors.

The standalone Run button checks the source and runs it with the existing Interpreter on a worker thread. Stop requests cancellation at interpreter instruction boundaries. Each run is limited to one million instructions and recursion depth 128. Console output and diagnostics have separate tabs. The game preview tab explains where to run engine scripts; it is not a second scene renderer.

The smart key picker targets the actual engine calls `input.isHeld("space")` and `input.wasPressed("space")`. It replaces only the string contents. Completion combines syntax tokens, declarations and identifiers parsed from the current file, parameters, standard-library names, and live engine API metadata. Hovering a candidate shows its available signature or documentation. Beginner mode changes messages and help only; it does not change parsing or execution.

## Build and package on Windows

Requirements: Windows 10/11 x64, Visual Studio 2022 C++ workload and Windows SDK, CMake 3.24+, Git, and network access for the first SDL3, Dear ImGui, and nlohmann/json fetch. OpenGL 3.3 is supplied by the graphics driver. LLVM is optional and disabled in the recommended build.

```powershell
cmake -S . -B build -A x64 -DJMENGINE_BUILD_ENGINE=ON -DJMENGINE_BUILD_SANDBOX=ON -DJMENGINE_ENABLE_LLVM=OFF
cmake --build build --config Release --parallel 2
ctest --test-dir build -C Release --output-on-failure
cmake --build build --target package --config Release
```

The ZIP contains `SamatStudio.exe`, the compiler CLI, and the v1.0 examples. MSVC uses the static C++ runtime. LLVM/JIT/AOT is an optional developer feature and is not in this initial Windows package. The GitHub Actions workflow builds and tests Windows x64 and creates this ZIP; a successful remote workflow is required before claiming a validated Windows build.

## Implemented and tested in this milestone

- Code and Korean parsers render through the shared AST; regression coverage enumerates every current `Statement::Kind` and `Expression::Kind` and fails when the corpus omits one.
- Korean print syntax removes the object particle from `값을 출력한다` / `값를 출력한다` before building the same `print(value)` call.
- Code-to-Korean-to-Code AST round-trips, type-check outcomes, and interpreter outputs are compared by regression tests.
- `.st` load/save helpers preserve UTF-8 bytes and reject other extensions or files above 16 MiB.
- The key picker range replacement, interpreter result transfer, and cancellation of an infinite loop are regression tested.
- All five standalone Studio samples are parsed, type checked, and executed by the Compiler regression test.

## Remaining limitations

- The editor uses Dear ImGui's multiline text widget. It does not yet provide token colors, a line-number gutter, auto-indent, bracket auto-closing, or source-level diagnostic underlines. The diagnostic pane shows the failing line and source excerpt.
- The file workflow uses a path field instead of a native file dialog.
- Completion is intentionally lightweight: incomplete syntax may prevent current-file symbols from being offered; scope filtering, full type inference, member completion, and a persistent language-server protocol are not implemented.
- The key picker currently covers Space, Enter, Escape, Tab, editing/navigation/modifier keys, A–Z, and 0–9. Other punctuation/media/keypad choices are not exposed.
- Beginner explanations cover a small set of common parser/runtime errors. User-function hover shows a signature; full parameter documentation is available only where metadata provides it.
- Interpreter runs are isolated from the UI thread and cancellable, but they are not a separate process. Crash isolation and memory quotas are not provided.
- The standalone Studio run path has no Scene/Input host context. Use the existing engine Play flow for Pong and engine APIs.
- GUI interactions have only render-smoke coverage here; mouse/keyboard workflows still need manual Windows review.
- Windows was not available in this environment. The Windows CI job and ZIP configuration have been added but have not yet executed successfully, so neither Windows runtime behavior nor the portable package is validated.
- The existing scene/game editor remains a prototype and is separate from the standalone Samat code editor. Therefore the requested complete beginner-first IDE and v1.0 release criteria are not met yet.
