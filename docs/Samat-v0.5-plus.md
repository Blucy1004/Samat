# Samat / 訓C正音 v0.5+ implementation report

This is the current capability report. The preceding v0.5 changes were reviewed, tested and committed as `b302b09`, then pushed normally to `main` before this implementation. No force push was used. This update is a substantial prioritized subset of the requested roadmap; unsupported features below are not advertised as implemented.

## 1. Baseline

The existing compiler, shared Code/Korean AST, interpreter, bootstrap x64, optional LLVM, editor and engine were retained. Baseline CTest passed before the prior commit/push.

## 2. Implemented scope

Numeric conversions, additional operators, native owned String/List runtime, typed List elements, interpreter Enum/Struct/Tuple/Range/Vector3/Color, file modules, standard-library additions, persistent execution sessions, typed JIT host callbacks, Float engine events and opt-in language-script editor Play.

## 3. Changes from v0.5

LLVM now executes String/List operations through a real C runtime ABI, including Linux AOT. The interpreter supports additional data models and the CLI resolves imports. Language-script Play retains state between events and restores the scene on Stop.

## 4. Files

Core changes: `LanguageCore.*`, `TypeChecker.cpp`, `SurfaceRenderer.cpp`, `JMIR.*`, `IRVerifier.cpp`, `LLVMBackend.cpp`, `X64Backend.cpp`, `StandardLibrary.*`. New runtime and modules: `RuntimeABI.h/.cpp`, `ModuleLoader.hpp/.cpp`. Host integration: `EngineScriptAPI.*`, `Application.*`, CLI and Sandbox entry points. Build/tests: `CMakeLists.txt`, `ExtendedRegression.cpp`, existing compiler/memory tests and benchmark. Samples: `examples/Samat/v05plus`.

## 5. AST

Both surfaces parse directly into the same AST. New nodes carry tuple literals, file imports, Enum/Struct declarations and typed List element annotations. Renderers preserve those constructs; no Korean-to-English source translation is used.

## 6. Type system

Primitive tag values remain stable. `List<Int/Float/Bool/String>` enforces elements across aliases, mutation and parameters/returns; Int promotes to Float when declared. Struct fields are checked at construction and mutation. Full generics, named-data function annotations, Optional flow analysis and complete static host checking remain unsupported. File/column diagnostic spans remain incomplete.

## 7. IR

JMIR remains backend-neutral. String constants, runtime calls, numeric conversion and bit operations were added. Native foreach lowers to control flow with a snapshot of collection identity and initial length. Typed List metadata drives lowering. Mixed unannotated Int/Float native literals are rejected rather than silently changing element arithmetic.

## 8. Verifier

Checks include runtime operation signatures, operands/results, new op types, dominance and typed external symbol identity. Runtime checks additionally enforce List element and bounds contracts. First-class collections still exceed bootstrap x64 capabilities.

## 9. Interpreter

Added immutable tuples/ranges/enum namespaces, shared mutable structs with cycle rejection, Vector2/3 arithmetic and Color values. Enum negative explicit initializers and ordinal overflow are incomplete. Struct methods, classes, destructuring, match/switch and Optional are not implemented.

## 10. Bootstrap x64

Retains integer/control-flow compilation, checked shifts and existing engine adapter support. String/List/data-model lowering is explicitly unsupported; there is no interpreter fallback presented as native execution.

## 11. LLVM JIT

Supports native String/List handles, conversions, runtime errors, globals for String, homogeneous List arguments/results, and typed host callbacks. List arguments are copied at the public C++ boundary; mutations do not propagate back into the caller's Value. Script-internal aliases share storage. List globals and String truth conditions remain unsupported. Host re-entry into the same native module is rejected instead of deadlocking.

## 12. LLVM AOT

Linux CLI links the installed sibling `libSamat_runtime.a`, followed by C++ and math libraries. Actual String/List executables and failing bounds executables were tested. Scalar Windows PE support is retained; Windows managed-runtime linking needs a Windows runtime archive and is explicitly unsupported. Managed-runtime COFF object emission alone does not imply a working executable.

## 13. Runtime ABI

Standalone static library exports fixed-width C functions and opaque handles, with context creation/activation, roots, retain/release and collection. Lists own their String children. Collection runs at invocation boundaries and reuses unreachable handles; allocations within a long invocation remain until that boundary. Borrowed String bytes and unretained handles must not outlive collection. Module execution is serialized. There is no moving or incremental GC.

## 14. FFI

Typed JIT callbacks accept Int/Float/Bool/String/Void, up to four arguments, validate arity and returns, preserve registry lifetime and propagate errors. Engine interpreter bindings implement equivalent host behavior. Typed List/Vector FFI and portable typed AOT host linking remain unsupported.

## 15. Standard library

Expanded math, String methods, homogeneous native Lists, seeded SplitMix64 random and monotonic time. Integer random ranges use rejection sampling. `print` and `println` both preserve legacy line-oriented behavior, joining arguments with spaces. CLI-only host IO provides readLine and file exists/read/write; core has no filesystem dependency. File read size is checked after reading, and permissions are those of the host process. Native IO is unsupported.

## 16. Korean surface

Direct parsing and rendering cover the new declarations, collections, operations and quoted imports. Paired samples were generated through the real renderer and executed; imports preserve their literal path. Canonical formatting does not retain comments. `^` retains legacy power behavior, `**` is right-associative power, and `bitXor` provides XOR; unary precedence retains the existing grammar.

## 17. Engine

Persistent start/update/fixedUpdate/input events, Float movement/jump, delta time, transform, vertical physics, input query and scene create/find/destroy/count bindings. Object IDs are resolved on every call. Scene numerics remain Float32 with finite/range checks. Camera, audio, animation, UI, signal and richer collision systems remain future work.

## 18. Editor / CLI

Opt-in language-script Play uses a persistent interpreter, starts once and restores the scene on Stop or failure. The legacy default remains. This editor preference is not serialized, and native Play selection is not exposed in the UI. CLI supports imports, canonical format and a persistent REPL with reset/help. REPL incremental semantic checking and installation of new Struct declarations remain incomplete. Stop is cooperative; asynchronous native interruption is not provided.

## 19. Tests

Existing tests remain enabled. Added seeded List differential execution at LLVM O0/O2, conversions, UTF-8, native runtime faults, ownership, typed callbacks, imports, sessions, actual Scene state changes and Linux AOT success/failure. The existing 64 seeded scalar differential cases remain. See validation results below for final counts.

## 20. Sanitizers

ASan/UBSan with leak detection tested in the LLVM-disabled standalone configuration. Runtime ownership tests repeatedly collect roots and aliases. This is not evidence of LLVM-enabled sanitizer/AOT compatibility or exhaustive fuzz coverage.

## 21. Capability matrix

PASS means the listed subset was executed in regression/manual validation. PARTIAL means the restrictions in this report apply. UNSUPPORTED means explicit rejection or no implementation. UNTESTED means no end-to-end execution evidence. AOT cells concern Linux unless specified.

| Feature | Interpreter | Bootstrap x64 | LLVM JIT | LLVM AOT |
|---|---|---|---|---|
| Int arithmetic/control flow | PASS | PASS | PASS | PASS |
| Float arithmetic | PASS | UNSUPPORTED | PASS | PASS |
| Numeric conversions | PASS | PARTIAL | PASS | PASS |
| Power/remainder/bit operations | PASS | PARTIAL | PASS | PARTIAL |
| UTF-8 String operations | PASS | UNSUPPORTED | PASS | PASS |
| Homogeneous List operations | PASS | UNSUPPORTED | PASS | PASS |
| Typed List parameters/returns | PASS | UNSUPPORTED | PASS | UNTESTED |
| Map | PASS | UNSUPPORTED | UNSUPPORTED | UNSUPPORTED |
| Tuple | PASS | UNSUPPORTED | UNSUPPORTED | UNSUPPORTED |
| Enum | PARTIAL | UNSUPPORTED | UNSUPPORTED | UNSUPPORTED |
| Struct | PARTIAL | UNSUPPORTED | UNSUPPORTED | UNSUPPORTED |
| Optional | UNSUPPORTED | UNSUPPORTED | UNSUPPORTED | UNSUPPORTED |
| Vector2/Vector3 | PARTIAL | UNSUPPORTED | UNSUPPORTED | UNSUPPORTED |
| Color | PARTIAL | UNSUPPORTED | UNSUPPORTED | UNSUPPORTED |
| Scalar range loops | PASS | PASS | PASS | PARTIAL |
| First-class Range | PASS | UNSUPPORTED | UNSUPPORTED | UNSUPPORTED |
| List foreach | PASS | UNSUPPORTED | PASS | PASS |
| Scalar globals | PASS | PASS | PASS | PASS |
| String/List globals | PASS | UNSUPPORTED | PARTIAL | UNTESTED |
| File modules | PARTIAL | UNTESTED | UNTESTED | UNTESTED |
| Math library | PASS | PARTIAL | PASS | PARTIAL |
| Seeded random/time | PASS | UNSUPPORTED | PASS | UNTESTED |
| Console/file IO | PARTIAL | UNSUPPORTED | PARTIAL | PARTIAL |
| Typed host FFI | PARTIAL | PARTIAL | PASS | UNSUPPORTED |
| Engine events/Float bindings | PASS | PARTIAL | PASS | UNSUPPORTED |
| Korean shared AST | PASS | PARTIAL | PASS | PASS |
| Windows executable | UNSUPPORTED | UNSUPPORTED | UNSUPPORTED | PARTIAL |

## 22. Samples

`native-collections.st/.jmk`: String/List and result 42. `data-model.st/.jmk`: Enum/Struct/Tuple/Vector3 and result 42. `modules/main.st/.jmk` plus utils: dependency loading and result 42. `engine-float-events.st/.jmk`: persistent Float engine events. Managed-native samples execute actual LLVM JIT/AOT; interpreter-only models are labeled by this matrix.

## 23. Performance

Debug-build exploratory measurements, ten invocations after warmup, under concurrent build load: integer accumulation Interpreter 3352.88 ms / bootstrap 0.508 ms / LLVM O0 0.667 ms / O2 0.064 ms; Float accumulation 8703.51 / unsupported / 0.454 / 0.387 ms; List workload 285.264 / unsupported / 2.972 / 1.905 ms. These are not production speed claims: load varies and optimizers can simplify arithmetic loops. Benchmark checks results and uses runtime arguments.

## 24. Unsupported scope

Native Map/Tuple/Enum/Struct/Vector/Optional, full generics, destructuring, classes/methods, match/switch, user module namespaces/packages, List globals in LLVM, native filesystem IO, complete Windows managed runtime, AOT typed host callbacks, complete editor persistence and the requested future component systems are not implemented.

## 25. Technical debt

Module initialization is deterministic dependency-first with once-only canonical identities, cycle checks and count/source budgets, but uses a flat global namespace and reports collisions. Runtime collection only occurs at public invocation boundaries. Static named-data checking, diagnostics spans, incremental REPL checking and full backend parity need further work. Host filesystem and callback permissions belong to the embedding application.

## 26. Next priorities

1. Define native Map/Tuple/Struct/Enum layouts and ownership before lowering them.
2. Add named semantic types, complete spans and incremental session checking.
3. Implement Optional and exhaustive matching.
4. Add qualified user modules and exported symbol rules.
5. Add Vector ABI and typed collection host callbacks.
6. Build/test Windows runtime archives and portable AOT host linking.
7. Add collection safe points and sustained allocation/fuzz tests.
8. Serialize language Play settings and design cooperative native cancellation.
9. Add collision/component events with editor end-to-end tests.

## Reproduction

The cloud setup script supplies CMake/Ninja, LLVM 19, graphics dependencies and SDL. In this workspace:

```bash
source /workspace/.tools/env.sh
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
./build/SamatCompiler run examples/Samat/v05plus/native-collections.st
```

Independent core builds use `-DJMENGINE_BUILD_ENGINE=OFF`; optional LLVM uses `-DJMENGINE_ENABLE_LLVM=OFF`; sanitizer builds additionally use `-DSamat_ENABLE_SANITIZERS=ON`. Consult CMake options for exact cache configuration. Install component `Samat` installs CLI and sibling runtime archive for AOT.

## Final validation

Results are recorded after final incremental rebuild: default engine/LLVM, LLVM-disabled engine, standalone CLI and ASan/UBSan suites; fresh build-directory checks, installed CLI AOT and GUI Play/Stop were also performed. Final suites: default engine/LLVM 4/4, LLVM-disabled engine 4/4, standalone CLI 3/3, ASan/UBSan 3/3. Compiler regression: 119 checks passed, 10 explicit capability skips. Extended regression: 66 checks passed. Korean GUI sample: Play/Stop and three SDL/OpenGL/ImGui frames passed. No force push was used.
