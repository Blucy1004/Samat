> Historical v0.5 milestone. See [the current v0.5+ implementation and capability report](Samat-v0.5-plus.md).

# Samat / 訓C正音 v0.5 development milestone

“사ᄅᆞᆷ의 ᄠᅳ디 CPU에 ᄉᆞᄆᆞᆺ게 ᄒᆞ노라.”

Samat is an independent language/toolchain. **訓C正音** is its structured Korean surface syntax (C: CPU / Code / Compiler / Computing). Both parsers construct the same `Program`, `Statement`, and `Expression` AST directly. Korean programs are never translated into English source for parsing, and user identifiers keep their spelling.

```text
Code ─────┐                         ┌─ AST Interpreter
          ├─ shared JM AST ─────────┤
訓C正音 ──┘                         └─ typed JM IR ─┬─ bootstrap x64 JIT
                                                  ├─ LLVM ORC JIT
                                                  └─ LLVM object/AOT
Engine modules ─ NativeFunctionRegistry / HostFunction ─ engine-owned Scene adapters
```

The AST interpreter executes development programs. JM IR remains independent of LLVM and has scalar types, locals, globals, calls, and explicit control-flow blocks. The bootstrap emitter is retained and now supports Windows x64 and Linux x86-64 C calling conventions. LLVM is an optional consumer of JM IR. JM Engine consumes the language through adapters; the language core has no Scene/physics dependencies.

## Build and CLI

`Samat::Core` is a standalone CMake library; the CLI links it without engine/SDL/OpenGL dependencies. Set `-DJMENGINE_BUILD_ENGINE=OFF` to build only the language, CLI and compiler/memory suites. The default build retains the complete engine/editor and `JMEngine::JMEngine` target.

Use CMake 3.24+, a C++20 compiler, Git, OpenGL development files, and the platform's SDL prerequisites for the engine configuration. SDL3, ImGui, and JSON dependencies retain their pinned repository versions. Install LLVM development packages (tested: LLVM 19.1.7), clang and lld for the LLVM/AOT path.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DJMENGINE_ENABLE_LLVM=ON
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
build/SamatCompiler --version
build/SamatCompiler run examples/Samat/v05/factorial.st
build/SamatCompiler check examples/Samat/v05/factorial.jmk
build/SamatCompiler --ast examples/Samat/v05/factorial.st
build/SamatCompiler --ir examples/Samat/v05/factorial.st
build/SamatCompiler --emit-llvm examples/Samat/v05/factorial.st
build/SamatCompiler --llvm-jit main examples/Samat/v05/factorial.st -O
build/SamatCompiler --native main examples/Samat/native-factorial.st
build/SamatCompiler --emit-obj factorial.o examples/Samat/v05/factorial.st
build/SamatCompiler build examples/Samat/v05/factorial.st -o factorial
build/SamatCompiler --build factorial.exe examples/Samat/v05/factorial.st --target x86_64-pc-windows-msvc
```

`LLVM_DIR` may point to an LLVM CMake package. `-DJMENGINE_ENABLE_LLVM=OFF` builds the interpreter and bootstrap without LLVM. Missing LLVM produces an explicit diagnostic for LLVM commands and disables the editor's LLVM buttons. `--linker <path>` overrides the detected clang/lld-link executable. No private workstation path is built into source.

Plain `SamatCompiler file` remains compatible. Interpreter `run` invokes `main()` when there are no top-level actions; `--entry name` selects an entry explicitly. Start handlers run with `eventName="start"`. Native JIT modes print the full language return value; AOT uses the operating system exit status. Linux truncates that status to 8 bits: use a program that checks a large result and returns 42 or 1, as the AOT regression test does. Windows AOT currently uses a freestanding `jm_entry` entry and lld-link, without a CRT; it can link scalar programs without external runtime/library dependencies.

## Types and functions

Values distinguish exact signed `Int` / i64, `Float` / f64, `Bool`, `String`, `Void`, `List`, `Map`, and `Vector2`. Lists are mutable heterogeneous `List<Value>`; generic `List<T>` syntax is not implemented. Untyped variables infer their initialized type; parameters and returns can be annotated:

```text
let speed: Float = 8.0
fn add(a: Int, b: Int) -> Int:
    return a + b
```

Integer literals preserve all 64 bits. `+`, `-`, `*`, and unary negation wrap modulo 2^64, matching the native backends. Integer division truncates toward zero. Zero division and `INT64_MIN / -1` produce diagnostics. Mixed Int/Float arithmetic promotes Int to Float; an explicit Float variable, parameter, or return can receive Int. This promotion may lose precision beyond 2^53. Bool/String do not silently convert to numeric values, Float does not silently narrow to Int, and string concatenation requires two Strings. Conditions retain the existing truth-value semantics.

Explicit scalar declarations without an initializer receive zero/false; String/List/Map receive empty values in the interpreter. An untyped uninitialized value is Void until assigned and is excluded from native lowering. Globals initialize in source order; native initializers reject reads of globals that are not initialized yet, including conservative checks through initializer function calls. Functions are collected before execution, so forward calls and mutual recursion work. Closures, nested functions, overloads, and default parameters are not supported.

Static `check` reports annotation mismatches, constant writes, duplicate declarations, arity errors, invalid loop control, and missing typed returns. Host values/functions are an open boundary, so unresolved host names and module availability can still require runtime validation. Source lines are tracked; exact token columns/end ranges remain incomplete. Native untyped parameters default to Int; scalar return types are inferred from expressions. Annotate parameters for Float-native function boundaries. Non-void native functions must return on every statically visible path.

## Control flow, collections, modules, and Korean syntax

`else if`, `while`, half-open ranges (`0..10` excludes 10), `for-in`, `break`, and `continue` execute in the interpreter. Scalar ranges, breaks, and continues lower into JM IR branches; continue targets a range's increment block. Foreach takes a snapshot of the list/map items, so mutation during iteration does not invalidate C++ iterators.

Lists support literal/read/write, `.length`, `push`/`append`, `pop`, and `clear`. Indices must be Int and bounds-checked; popping an empty list is an error. Maps and Vector2 retain their existing interpreter functionality. Strings support owned literals, variables, equality/comparison, concatenation, byte indexing/length, and captured `print`/`println` output. UTF-8 String length/index are **bytes**, not graphemes.

`import jm.math`, `jm.console`, and `jm.collections` provide standard module identities. External modules are resolved through `RunOptions::moduleResolver` for interpreter execution and `NativeFunctionRegistry::registerModule` for JIT execution. `jm.game` is registered by the engine adapter; the language core does not know that engine module. The initial resolver supports registered identities, not file/package loading. Object emission records imports for a later runtime/link integration; standalone `build` currently rejects external module dependencies explicitly. Standard math descriptors have stable `builtin.math.*` identities distinct from the syntax. Native LLVM math supports abs/min/max/clamp/sqrt/pow/sin/cos/round/floor/ceil; the interpreter additionally retains assert, length, tan, vector2/color, print, and list helpers. User-defined functions can shadow unqualified library names. Standard math calls currently require positional arguments.

Korean parsing is a deterministic structured DSL. Expressions retain common operator/index/call syntax. Examples:

```text
정수 변수 score를 0으로 정한다.
실수 변수 speed를 8.0으로 정한다.
문자열 변수 name을 "JM"으로 정한다.
논리 변수 alive를 참으로 정한다.
목록 변수 values를 [1, 2, 3]으로 정한다.
values의 0번째 값을 10으로 정한다.
0부터 10까지 i를 반복하며:
    i를 출력한다.
```

Typed functions use `함수 add(a: Int, b: Int) -> Int:`. `반복을 멈춘다.` and `다음 반복을 진행한다.` represent break/continue; `실행한다 expression` represents an expression statement. Native/shared examples live under `examples/Samat/v05`; English and Korean samples are rendered from the same AST. The renderers cover the scalar, collection, function, import, and event grammar and preserve identifier spelling. Hangul particles use Unicode syllable decomposition, including 길로 / 집으로. This is not a natural-language/NLP parser.

## Engine APIs and events

`EngineScriptAPI.hpp/.cpp` owns all Scene access. `engineHostFunctions(context)` supplies the interpreter bindings; use `options.moduleResolver = engineModuleResolver()` for game imports. `engineNativeFunctions()` registers the engine module and supplies the same stable symbols and metadata to either native backend. A scoped `EngineScriptScope` activates the native context on the calling thread and restores the previous context. Keep it alive around native execution. Objects are resolved by ID each call rather than storing pointers into the Scene vector.

The initial four-i64 FFI supports player move/jump/grounded, key held/pressed (0 left, 1 right, 2 space), `time.deltaMicros`, and `transform.xMilli`. Movement uses the context's frame delta and modifies the actual Scene object. Jump applies velocity only when grounded. The scalar bridge requires Int arguments, matching interpreter and native behavior; float-valued engine APIs, audio, spawn/destroy, and general typed/boxed FFI are future work. Function metadata includes stable ID-derived symbol, display names, documentation, parameter names/types, and return type. Named arguments must match metadata order. Native callbacks must not throw across the bootstrap's generated frames. Missing context/player returns -1 for actions; movement/jump success returns 1 and airborne jump returns 0.

`on start:`, `on key.right.held:`, and `on key.space.pressed:` are shared Event AST nodes. Korean headers (`시작할 때:`, `오른쪽 키를 누르는 동안:`, `스페이스 키를 눌렀을 때:`) create those same nodes. Other stable event names use `이벤트 event.name:`. IR lowering generates internal Void functions and an explicit event-registration table. LLVM object/IR output also exports C-layout `__jm_event_table` (pairs of event-name pointer and Void function pointer) and `__jm_event_count`. The engine host dispatches `executeNativeEvent(module, code, event)` within a context scope. The existing editor project's ScriptDocument/bytecode play path is preserved; adoption of the new native event path as the default game runtime remains a separate integration milestone.

## Memory and execution safety

Strings are value-owned C++ implementation objects inside the interpreter; no std::string ABI is exposed to native code. Lists/maps use shared ownership for aliases; inserting an object that would create a direct or indirect collection cycle is rejected. This avoids reference-count leaks and recursive stringify/equality crashes for language-created objects. Pointers are not exposed to scripts. A future managed runtime/GC can replace this interpreter representation without changing the surface AST or JM IR contract.

`RunOptions` retains instruction budget, recursion limit, injected initial values, and cooperative cancellation, plus entry/event selection. Each loop has cancellation/budget checks. Native release execution has no mandatory loop/recursion budget. Bootstrap arithmetic helpers propagate fault flags back to the invocation boundary; LLVM JIT uses unwindable runtime error calls. Linux AOT arithmetic faults print an error and exit 1. Freestanding Windows AOT arithmetic-fault paths currently trap; they are a limitation, not successful execution. Host exception unwinding is tested for LLVM on Linux; Windows LLVM JIT unwinding has not been validated here.

The IR verifier checks canonical blocks, terminators, branch targets, unique/defined values, dominance, local/global access, value types, calls/arity, returns, and event bindings. JM constant folding is independent of LLVM O2. Unsafe constant divisions remain in the IR to preserve diagnostics. Optimized/unoptimized results are compared in tests.

## Validation and limits

`jmengine_smoke` retains movement/jump, physics, project persistence, particles, Korean/Code AST checks, integer native recursion, and C++ callback tests. `Samat_runtime_memory` additionally checks escaping aliases, foreach mutation, cycle rejection and repeated destruction; it was also run with AddressSanitizer, UndefinedBehaviorSanitizer and leak detection. `Samat_compiler_regression` adds type/collection/runtime errors, ranges/control flow/globals, scalar LLVM parity, round trips, IR rejection tests, real engine event adapters, 64 seeded bounded differential programs, object emission, and a standalone AOT factorial check. Capability skips are printed explicitly. LLVM-off builds execute interpreter/bootstrap checks and skip LLVM/AOT.

```sh
cmake -S . -B build -DJMENGINE_BUILD_BENCHMARKS=ON
cmake --build build --parallel 4
build/Samat_benchmark
build/jmengine_sandbox --smoke-frames 3
```

The benchmark reports Debug/Release, warms up lazy JIT compilation before timing, validates results, and measures a constant-input integer loop. LLVM may eliminate/fold the loop; these numbers are not a general performance claim. The bounded sandbox smoke starts SDL, OpenGL 3.3 and ImGui, renders frames, checks OpenGL errors, and exits. A real display or Xvfb/software Mesa is required.

| Feature | Interpreter | Bootstrap x64 | LLVM JIT | LLVM AOT |
|---|---|---|---|---|
| Int / Bool | Yes | Yes | Yes | Yes |
| Float | Yes | Capability diagnostic | Yes | Yes |
| String / List / Map | Yes | Capability diagnostic | Capability diagnostic | Capability diagnostic |
| Globals | Yes | Capability diagnostic | Yes | Yes |
| if/else, while, range for, break/continue | Yes | Yes | Yes | Yes |
| Functions / recursion / mutual recursion | Yes | Yes | Yes | Yes |
| stdlib math | Yes | Needs explicit bindings / Float unsupported | Listed scalar math functions | Host math runtime; Windows runtime-dependent calls may fail linking |
| Registered C++ / engine calls | Yes | Four-i64 ABI | Four-i64 ABI | External runtime objects needed |
| Shared event AST / handlers | Yes | Yes | Yes | Table/functions emitted; external dispatcher needed |

Windows PE linking and COFF emission are verified in the Linux onboarding environment; running that PE and Windows ABI regression checks requires Windows. String/List LLVM runtime, file imports, general float/boxed FFI, native GC, debugger/editor asynchronous Stop, and full metadata-driven completion remain unimplemented. There is no claim that these capability gaps are LLVM/environment failures.

## First five v0.6 priorities

1. Define a portable managed String/List runtime ABI and lower ownership/indexing into JM IR/LLVM.
2. Add fully typed FFI declarations and Float/boxed bindings while keeping stable symbols and engine-owned modules.
3. Integrate the new event dispatcher into editor Play with persistent globals and asynchronous cancellation.
4. Add file imports and complete name/source-range resolution to the registered-module resolver.
5. Run Windows CI for bootstrap ABI, LLVM exception/unwind behavior and AOT process entry/error handling.
