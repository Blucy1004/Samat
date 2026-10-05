# Samat Language Core

For the current v0.5 types, syntax, safety, and backend capabilities, see [Samat v0.5](Samat-v0.5.md). The historical milestone description follows.

Samat now has a standalone calculation runtime in `jm::script`. It does not depend on a running JM Engine scene. The engine-facing runtime can provide host functions through `HostFunction`; the value model and parser remain usable without those APIs.

## Syntax and runtime path

Both `parseCode` and `parseKorean` produce the same `Program` and `Statement` AST types. `execute` runs that AST with mutable values and lexical block environments. Korean Syntax constructs the shared AST directly and does not create translated Code Syntax source or a second AST format.

The Code syntax supports `let` and `const`, arithmetic/comparison/logical expressions, lists and maps, indexed/member assignment, `if`/`else`, runtime `while`, range and collection `for`, functions, parameters, return, recursion, break, and continue. Built-ins include `print`, `assert`, `len`, common math functions, `vector2`, and `color`. Lists provide `append`, `push`, `pop`, and `clear`.

The initial 訓機正音 subset supports numeric variable declarations, `함수 name(args):`, if/else and while headers using Korean comparison phrases, arithmetic returns, increments/decrements, and adding a value to a variable. It shares the same AST/runtime. This is an initial syntax subset; the complete Code grammar has not yet been mirrored in Korean.

## Safety

`RunOptions` has a configurable instruction budget, recursion limit, initial runtime values (for inputs such as `n`), and cooperative `shouldStop` callback. Infinite loops remain valid syntax and execute until a host safety limit or stop request ends execution. A host application can connect its editor Stop button or watchdog to `shouldStop`.

## Executable samples in the smoke suite

`tests/EngineSmoke.cpp` runs programs through parse → AST → execute, including:

- mutable `while` loop (0 through 100) and a summation loop using a runtime-injected input value
- factorial, Fibonacci, and Euclidean GCD recursion
- list append/read/indexed write and map member read/write
- Bubble Sort and FizzBuzz from 1 to 100, with captured output checks
- range/collection `for`, lexical shadowing, logical expressions, instruction budget, and recursion limit
- Korean and Code spellings of the same while program, compared structurally and executed
- the existing Player movement and jump simulation regression

Build and run the suite with CMake/CTest. The v0.5 runtime now separates exact Int/i64 values and Float/f64; and it does not yet implement closures, nested function declarations, input streams, or a debugger UI. These are follow-on language/runtime features, not engine-only commands.

An experimental AST → JM IR → executable x86-64 backend is available for a strict integer subset. See [Samat Native Compilation](Samat-Native-Compilation.md) for the target boundary, CLI commands, parity tests, and current limitations.
