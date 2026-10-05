JOSAMOSA ENGINE / JM Engine development build

Run JMEngine.exe to open the editor.
2D Play controls: hold Left/Right Arrow to move; press Space to jump.

In the Code workspace, choose `게임 스크립트` for the existing Player movement/jump events, or `Samat 코어` for general-purpose Code syntax. The core panel includes compile checking, run/output, Factorial and FizzBuzz starters, an available-code inventory, and Ctrl+Space/Tab completion. The Korean game-script editor remains available in the game-script tab.

Samat.exe is also a standalone command-line runner for `.st` Code syntax and the initial `.jmk` 訓機正音 subset:

  Samat.exe examples\fizzbuzz.st
  Samat.exe examples\factorial.jmk

The shared runtime supports mutable values, nested expressions, if/else, runtime while, range/collection for-loops, functions and recursion, lists/maps with runtime indexing and mutation, runtime-injected values through the C++ API, instruction budgets, cooperative cancellation, and recursion diagnostics. CTest verifies while/summation, factorial, Fibonacci, GCD, collection operations, Bubble Sort, FizzBuzz (1–100), safety limits, Korean factorial, and Player movement/jump regression.

Known limits: Korean language-core syntax is an initial subset; numeric values use double with no distinct Integer type; closures, external input streams, debugger UI, and saving the general-purpose editor buffer into a project are not implemented yet. Existing game-script source is saved in the project as before.

Source docs: docs/Samat-Language-Core.md
