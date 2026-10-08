# JOSAMOSA ENGINE (JM Engine)

> 사람의 뜻이 CPU에 사맛게 하노라.

JM Engine is a lightweight C++20 engine project for building both 2D and 3D games. It uses SDL3 for the window and input, and OpenGL 3.3 for rendering. The goal is a small, focused engine that grows feature by feature, rather than a full production editor from day one.

## Samat / 訓C正音 v0.7 — Safe & Live (partial milestone)

Optional values, validated Entity references and compatible live program generations now share the Code/Korean AST and runtime. Invalid live candidates retain the previous generation and compatible state. Engine API metadata drives search, templates, help and numeric presets. See [v0.7 implementation, validation and capability matrix](docs/Samat-v0.7.md) for the tested subset and explicit limits; [v0.6](docs/Samat-v0.6.md) is the preceding checkpoint.

## Samat v0.8 — write programs, build games

Samat reads `.st` files directly. Try the interactive calculator with `build/SamatCompiler run main.st`, entering `7`, `*`, and `7` on separate lines. The starter game is [examples/Samat/pong.st](examples/Samat/pong.st); run it in the engine editor with `build/jmengine_sandbox --smoke-language examples/Samat/pong.st` or open the source in the Code workspace and press Play.

**Samat (`.st`) describes behavior. Haerye (`.hy`) describes scenes and appearance.** Haerye v0.1 can load declarative rectangles and circles into the existing JME Scene; see [docs/Haerye-v0.1.md](docs/Haerye-v0.1.md) and the [Pong pair](examples/Pong/).

## Samat Studio — v1.0 work in progress

The **Samat Studio** tab adds standalone `.st` editing, line numbers and Samat/訓C正音 syntax colors, automatic indentation and bracket pairing, document shortcuts, Windows native file dialogs, Code/訓C正音 conversion, metadata-assisted completion, a key picker for `input.isHeld` / `input.wasPressed`, capped cancellable interpreter execution, and console/diagnostic panes. The existing game editor and Pong Play path remain separate. See [the implementation status and limits](docs/SamatStudio-v1.0.md); the current work does **not** meet the full v1.0 release criteria yet.

## Engine editor vertical slice

The sandbox opens a resizable editor with **Scene**, **Code**, and **Game Preview** tabs. The starter scene contains a controllable 2D Player and a static floor, alongside a separate 3D cube scene view. Press Play and use Left/Right to move and Space to jump. A fixed-step prototype physics pass handles gravity and platform collisions. Stop restores the scene to its pre-play state. The editor can create, open, and save projects as readable `project.jm`, `scenes/main.scene`, and `scripts/main.samat.json` files. The Code tab has an editable Samat surface with numeric `let`/`const` declarations, parameterized `fn` functions, arithmetic expressions for numeric action values, event handlers, and a small set of object actions. Function calls and parameters lower into the same event/action AST as 訓機正音, and both surfaces can be parsed back without changing their meaning. Scene objects have visibility and five draw layers; 2D sprites render from lower layer to higher layer. Korean UI uses the installed Windows Malgun font when available. 3D cubes retain depth testing and simple directional lighting.

### Controls

- Select **2D Scene** or **3D Scene** in the toolbar. The current script runtime is for 2D scenes.
- 2D Scene: middle mouse drag to pan; wheel to zoom
- 3D Scene: left mouse drag to orbit; wheel to zoom
- `F5`: Play; `Shift+F5`: Stop; `Delete`: remove the selected object
- `Ctrl+S`: save the current project
- In Play mode, hold `Left/Right Arrow` to move and press `Space` to jump
- `Esc`: close outside Play; during Play it can be received as a script key

The toolbar switches dimensions and starts or stops Play. The hierarchy only shows objects for the selected dimension. Use its create button to add a 2D object or 3D cube, then edit its layer and properties in the Inspector.

The renderer loads its small set of OpenGL functions through SDL after the context is made current. OpenGL 3.3 is the minimum graphics API target.

## Build

Requirements: CMake 3.24+, a C/C++20 compiler, Git, and an internet connection for the first dependency fetch.

```powershell
cmake -S . -B build -A x64 -DJMENGINE_ENABLE_LLVM=OFF
cmake --build build --config Release --parallel 2
ctest --test-dir build -C Release --output-on-failure
./build/Release/SamatStudio.exe
cmake --build build --target package --config Release
```

The Windows command targets Visual Studio 2022 x64. The generated portable ZIP includes `SamatStudio.exe`, the compiler CLI, and Studio examples. `jmengine_sandbox.exe` remains as a compatibility copy next to the Studio executable. CMake fetches SDL3 3.2.10, Dear ImGui 1.92.9b, and nlohmann/json 3.12.0; OpenGL is provided by the platform. The [Windows CI workflow](.github/workflows/samat-studio-windows.yml) builds, tests, and packages the x64 app. This Linux environment has not run that workflow or a Windows build.

Run the automated regression suite with `ctest --test-dir build -C Release --output-on-failure`. It includes engine smoke, Hangul and Samat/Korean AST round-trips, sample execution, file and syntax helpers, key replacement, streaming interpreter output and cancellation checks, plus compiler/backend/runtime regressions. `Ctrl+S` saves the `.st` document in Samat Studio and the project in the game editor.

## Structure and growth path

```text
src/
├── JMEngine/
│   ├── Core/       Application lifecycle and main loop
│   ├── Renderer/   Shared OpenGL renderer, mesh data, and matrix math
│   ├── Physics/    Future Box2D integration
│   ├── Scene/      Scene objects and transforms
│   ├── Project/    Versioned project and scene JSON files
│   ├── Input/      Future input actions and mappings
│   ├── Audio/      Future SDL audio services
│   └── Editor/     ImGui hierarchy, inspector, and toolbar
└── Sandbox/        Small host application for engine development
```

Growth path: **native data-model parity → qualified modules and editor persistence → collision events and richer components → Tilemap → Animation → asset workflow**. Current code deliberately supports a small explicit grammar and an interpreter executes its compiled event instructions. 2D and 3D share the renderer and object model; their projections and transforms select how geometry is drawn.

Samat also has an experimental AST → JM IR → x86-64 in-memory native compiler for integer/control-flow functions. The interpreter remains the normal edit-and-run path. See [Samat Native Compilation](docs/Samat-Native-Compilation.md) for the CLI, native subset, and limitations.

The original engine milestone scope is described below; consult the v0.6 capability document for current language support.

The long-term product architecture, Samat design, Korean/code editor model, runtime, storage format, security boundaries, and milestones are described in [docs/JM_ENGINE_ARCHITECTURE.md](docs/JM_ENGINE_ARCHITECTURE.md). This prototype supports numbers, named parameters, `+ - * /`, parenthesized numeric expressions, function calls, and start/held/pressed input events. The legacy engine ScriptDocument play path still has a limited action grammar; the independent language core supports String/Bool/List/Map, general branches, loops and return values. Collision events and full editor undo/redo are not implemented yet. The 3D view is a scene-editing preview; the current Samat game runtime and platform physics are 2D-only.
