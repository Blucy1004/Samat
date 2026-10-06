# Samat v0.8: Ground Floor

Samat is the `.st` source form used by the JM language. The CLI parses it into
the shared JM AST; it does not translate the program through C++ or a second
language implementation.

## Run the example

Build the project with `JMENGINE_BUILD_ENGINE=ON`, then open the `.st` file
in the editor's Code workspace and press Play. The headless smoke runner is
also available for scripted startup:

```sh
build/jmengine_sandbox --smoke-language examples/Samat/pong.st
```

The standalone `SamatCompiler` runs `.st` files that use the standalone
language/console host; it does not attach scene and input APIs.

The Pong example spawns three Sprite2D objects, moves the paddles with W/S and
Up/Down, reflects the ball at the paddles and playfield edges, tracks scores,
and resets the score with Space. Its collision and scoring rules are ordinary
Samat code using reusable scene, input, math, random, and time APIs.

## Available in this increment

- The standalone Runner accepts LF, CRLF, and input without a final newline.
- `input.wasPressed(key)` complements the existing `input.isHeld(key)` API.
- `scene.spawn(name, position, scale, color)` creates a Sprite2D and returns
  `Entity?`. Entity handles expose position, scale, rotation, `setPosition`,
  `setScale`, `setRotation`, and `setColor`. Resolution checks scene identity
  and entity generation.
- `bool(value)` converts Bool, Int, Float, and String using language truthiness.
- String `slice(start, count)` aliases the existing `substring` operation.
- `.st` and Korean spellings parse to the same AST. Engine native calls use
  stable registry metadata shared by type checking and the execution backends.

## Backend support

The Pong example is verified on the Interpreter and LLVM engine backends.
Engine-native APIs require an engine runtime; standalone CLI native AOT does
not provide the scene/input host bindings. Bootstrap x64 also does not support
all engine-native calls. Audio playback, a script camera API, and general
rigid-body collision APIs remain unsupported; Pong handles its simple paddle
collision in Samat code.

String slicing uses the same byte-index behavior as `substring`; indices are
not Unicode grapheme offsets. `scene.spawn` creates a visible Sprite2D, not a
textured asset or audio object.
