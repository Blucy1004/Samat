# JOSAMOSA ENGINE (JM Engine)

JM Engine is a lightweight C++20 engine project for building both 2D and 3D games. It uses SDL3 for the window and input, and OpenGL 3.3 for rendering. The goal is a small, focused engine that grows feature by feature, rather than a full production editor from day one.

## Samat / 訓C正音 v0.6 development milestone

The native data update extends LLVM JIT/Linux AOT with Struct fields, Vector2/3, Color, Tuple, stepped Range and typed String-key Map execution. Typed List/Vector callbacks connect actual Scene properties to persistent engine events. Interpreter, bootstrap x64, optional LLVM and independent core builds remain. See [v0.6 implementation, validation and capability matrix](docs/Samat-v0.6.md); [v0.5+](docs/Samat-v0.5-plus.md) is the preceding checkpoint.

## Engine editor vertical slice

The sandbox opens a resizable editor with **Scene**, **Code**, and **Game Preview** tabs. The starter scene contains a controllable 2D Player and a static floor, alongside a separate 3D cube scene view. Press Play and use Left/Right to move and Space to jump. A fixed-step prototype physics pass handles gravity and platform collisions. Stop restores the scene to its pre-play state. The editor can create, open, and save projects as readable `project.jm`, `scenes/main.scene`, and `scripts/main.samat.json` files. The Code tab has an editable Samat surface with numeric `let`/`const` declarations, parameterized `fn` functions, arithmetic expressions for numeric action values, event handlers, and a small set of object actions. Function calls and parameters lower into the same event/action AST as 訓機正音, and both surfaces can be parsed back without changing their meaning. Scene objects have visibility and five draw layers; 2D sprites render from lower layer to higher layer. Korean UI uses the installed Windows Malgun font when available. 3D cubes retain depth testing and simple directional lighting.

### Controls

- Select **2D Scene** or **3D Scene** in the toolbar. The current script runtime is for 2D scenes.
- 2D Scene: middle mouse drag to pan; wheel to zoom
- 3D Scene: left mouse drag to orbit; wheel to zoom
- `F5`: Play; `Shift+F5`: Stop; `Delete`: remove the selected object
- `Ctrl+S`: save the current project
- In Play mode, hold `Left/Right Arrow` to move and press `Space` to jump
- `Esc`: close

The toolbar switches dimensions and starts or stops Play. The hierarchy only shows objects for the selected dimension. Use its create button to add a 2D object or 3D cube, then edit its layer and properties in the Inspector.

The renderer loads its small set of OpenGL functions through SDL after the context is made current. OpenGL 3.3 is the minimum graphics API target.

## Build

Requirements: CMake 3.24+, a C/C++20 compiler, Git, and an internet connection for the first dependency fetch.

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug
./build/Debug/jmengine_sandbox.exe
```

On generators that place executables directly in `build`, run `./build/jmengine_sandbox.exe`. CMake fetches SDL3 3.2.10, Dear ImGui 1.92.9b, and nlohmann/json 3.12.0. OpenGL is provided by the platform. Current physics is a deliberately small built-in platformer solver; Box2D, audio services, textures, tilemaps, and animation are planned additions.

Run the automated smoke simulation with `ctest --test-dir build -C Debug --output-on-failure`. It checks viewport scaling, Hangul particles, Korean/Code AST round-trips, movement, floor collision, jumping, and project save/load.

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
