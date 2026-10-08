# Samat v1.0 / Samat Studio — implementation status

**사람의 뜻이 CPU에 사맛게 하노라.**

This is a release-hardening milestone on top of the Samat 0.8 language core. It does not claim v1.0 release readiness until the Windows CI run and hands-on GUI scenarios below are verified. The editor reuses the existing Samat parser, checker, interpreter, engine metadata, and Code/訓C正音 AST. The editor widget is ImGuiColorTextEdit, pinned from `ocornut/imgui_club` companion repository `BalazsJako/ImGuiColorTextEdit` at commit `ca2f9f1462e3b60e56351bc466acda448c5ea50d`; its MIT license ships in the ZIP.

## Use the Studio

Build and launch from the repository root so the example paths resolve:

```powershell
cmake -S . -B build -A x64 -DJMENGINE_ENABLE_LLVM=OFF
cmake --build build --config Release --parallel 2
./build/Release/SamatStudio.exe
```

The older `jmengine_sandbox.exe` filename is also copied beside `SamatStudio.exe` for existing scripts. In the **Samat Studio** tab, Windows Open/Save As and Ctrl+O/Ctrl+S use native `.st` dialogs. Other platforms use the path field. Ctrl+N creates a document. The editor provides line numbers, Samat/訓C正音 token colors, indentation after block markers, bracket and quote pairing, Tab completion, and Shift+Tab outdent. The sample menu includes Hello World, a calculator, loops, a function example, a Korean function example, and the existing Pong scene script. Samples open as unsaved copies so editing them cannot overwrite the repository examples. Open accepts invalid source so it can be corrected; Save writes the text as entered, even when it has syntax errors. Dirty documents warn before opening a file, changing to a new file, or closing the window.

The standalone Run button checks the source and runs it with the existing Interpreter on a worker thread. A second run is refused while one is active. Stop requests cancellation at interpreter instruction boundaries. Each run is limited to one million instructions, recursion depth 128, and one MiB of printed output. Print output is forwarded to the UI as it is produced; interpreter failures appear in Diagnostics. The game preview tab explains where to run engine scripts; it is not a second scene renderer.

The smart key picker targets the actual engine calls `input.isHeld("space")` and `input.wasPressed("space")`. It replaces only the string contents. Completion combines syntax tokens, declarations and identifiers parsed from the current file, parameters, standard-library names, and live engine API metadata. Hovering a candidate shows its available signature or documentation. Beginner mode changes messages and help only; it does not change parsing or execution.

## Build and package on Windows

Requirements: Windows 10/11 x64, Visual Studio 2022 C++ workload and Windows SDK, CMake 3.24+, Git, and network access for the first SDL3, Dear ImGui, and nlohmann/json fetch. OpenGL 3.3 is supplied by the graphics driver. LLVM is optional and disabled in the recommended build.

```powershell
cmake -S . -B build -A x64 -DJMENGINE_BUILD_ENGINE=ON -DJMENGINE_BUILD_SANDBOX=ON -DJMENGINE_ENABLE_LLVM=OFF
cmake --build build --config Release --parallel 2
ctest --test-dir build -C Release --output-on-failure
cmake --build build --target package --config Release
```

The ZIP contains `SamatStudio.exe`, its legacy alias, the compiler CLI, runtime archive, v1.0 examples, and the editor license. CMake collects non-system DLL dependencies of the Studio and CLI during install. MSVC uses the static C++ runtime. LLVM/JIT/AOT is an optional developer feature and is not in this initial Windows package. The GitHub Actions workflow builds/tests Windows x64, inspects the ZIP, and starts the packaged CLI. A successful remote workflow is required before claiming a validated Windows build.

## Implemented and tested in this milestone

- Code and Korean parsers render through the shared AST; regression coverage enumerates every current `Statement::Kind` and `Expression::Kind` and fails when the corpus omits one.
- The editor's lexer-based syntax colors distinguish keywords, built-ins, numbers, operators, strings, and `#` comments. Regression checks ensure keywords and comment markers inside single- or double-quoted strings remain string tokens.
- Korean print syntax removes the object particle from `값을 출력한다` / `값를 출력한다` before building the same `print(value)` call.
- Code-to-Korean-to-Code AST round-trips, type-check outcomes, and interpreter outputs are compared by regression tests.
- `.st` load/save helpers preserve UTF-8 bytes and reject other extensions or files above 16 MiB.
- Worker tests cover duplicate-run refusal, incremental output delivery, and output-limit errors.
- The key picker range replacement, interpreter result transfer, and cancellation of an infinite loop are regression tested.
- All five standalone Studio samples are parsed, type checked, and executed by the Compiler regression test.

## Release interaction checklist

| Scenario | Result in this environment |
| --- | --- |
| Start Studio, create a file, write Hello World, run it | Not driven through the UI |
| Open, edit, save, and rerun an existing `.st` file | Not driven through the UI |
| Show a syntax error, fix it, and rerun | Parser/type-check regressions pass; UI flow not driven |
| Enter a function with autocomplete | Not driven through the UI |
| Change the key picker from Space to Enter | Key range helper is regression tested; UI flow not driven |
| Toggle Beginner mode | Not driven through the UI |
| Stop an infinite loop | Worker cancellation is regression tested; UI flow not driven |
| Parse and run the Korean example | Automated Compiler regression passed; UI flow not driven |

## Remaining limitations

- The editor widget is a separately maintained upstream work-in-progress; its behavior and keyboard interactions need hands-on review on Windows.
- Source-level diagnostic underlines are not implemented. The editor marks the failing line and the diagnostic pane shows its source excerpt.
- Native file dialogs are implemented only on Windows. Linux and other platforms use the path field.
- Completion is intentionally lightweight: incomplete syntax may prevent current-file symbols from being offered; scope filtering, full type inference, member completion, and a persistent language-server protocol are not implemented.
- The key picker currently covers Space, Enter, Escape, Tab, editing/navigation/modifier keys, A–Z, and 0–9. Other punctuation/media/keypad choices are not exposed.
- Beginner explanations cover a small set of common parser/runtime errors. User-function hover shows a signature; full parameter documentation is available only where metadata provides it.
- Interpreter runs are isolated from the UI thread and cancellable, but they are not a separate process. A native crash can still terminate Studio. The instruction and output limits do not bound heap usage or wall-clock time spent by a single native function.
- The in-process interpreter has no process exit status, separate stderr stream, or OS-level process handle to terminate. Runtime errors are shown in Diagnostics and `print` output is shown in Console.
- The standalone Studio run path has no Scene/Input host context. Use the existing engine Play flow for Pong and engine APIs.
- GUI interactions have only render-smoke coverage here; none of the eight manual Studio scenarios in the release checklist were driven end-to-end through the UI in this environment. The Korean example was parsed, type checked, and run in automated tests.
- Windows was not available in this environment. The Windows CI job, dependency collection, and ZIP inspection checks have been added but have not yet executed successfully, so Windows runtime behavior and the portable package remain unvalidated.
- The existing scene/game editor remains a prototype and is separate from the standalone Samat code editor. Therefore the requested complete beginner-first IDE and v1.0 release criteria are not met yet.
