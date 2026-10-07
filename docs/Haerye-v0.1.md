# Haerye v0.1

Haerye (해례) is JME's declarative scene language. Its canonical extension is
`.hy`. Samat (`.st`) describes actions and behavior; Haerye describes the
objects in a Scene and how they look. Haerye has no variables, functions,
control flow, expressions, or embedded Samat.

## Pong example

The split example is [pong.hy](../examples/Pong/pong.hy) plus
[pong.st](../examples/Pong/pong.st). Haerye creates the named paddles and ball;
Samat finds them with `scene.find` and moves them during updates.

```sh
build/jmengine_sandbox --haerye examples/Pong/pong.hy --language examples/Pong/pong.st
```

This opens the JME editor with the Haerye Scene loaded and the Samat program
running. To inspect just the declared scene, omit `--language`.

## v0.1 syntax

```hy
scene "Pong" {
    background: "#101014"

    object "player" {
        shape: rectangle
        position: (-8, 0)
        size: (0.5, 3)
        rotation: 0
        color: "#FFFFFF"
    }

    object "ball" {
        shape: circle
        position: (0, 0)
        size: (0.5, 0.5)
        color: "#FFFFFF"
    }
}
```

Supported declarations are one `scene`, an optional `background`, and named
`object` blocks. Objects require `shape` (`rectangle` or `circle`), `position`,
positive `size`, and `color`; `rotation` defaults to zero degrees. Colors use
`#RRGGBB`. Vectors are numeric `(x, y)` pairs. `//` comments are supported.
The parser reports syntax locations and rejects duplicate objects, duplicate
properties, unknown properties, invalid shapes, malformed vectors/colors,
missing required values, and extra/nested declarations. A `sprite`, `texture`,
or `asset` property reports that assets are unsupported.

## Scene integration

The dedicated Haerye parser creates a typed Scene description, validation checks
it, and instantiation replaces objects in the existing JME `Scene`. Objects are
ordinary `Sprite2D` GameObjects with stable IDs derived from their declared
names and fresh Scene generation tokens, so `scene.find()` and stale Entity
checks continue to use the existing runtime safeguards. Rectangles and circles
use JME's existing 2D renderer.
Scene name and background color, object shape, and transforms are retained by
the existing project serializer.

`serializeHaerye` emits a canonical form that parses back to an equal semantic
description. Whitespace and comments are not preserved.

## Limits and future work

Haerye v0.1 supports only rectangle and circle primitives. Asset import,
textures, audio, models, prefabs, visual editor synchronization, and hot reload
are deferred. The editor can load a `.hy` file at launch through `--haerye`;
drag-and-drop editing and saving back to Haerye are future editor work.
