# qiven-context-v3

Qiven Context v3: a native C++20 core that keeps a person's working context
as entries in a local SQLite store, at a directory its user chooses; selects
what a session reads from it; forgets on request; and seals the store's
history into self-verifying bundles, which a separate reference reader
checks.

Status: under construction. Nothing here is released.

## Specification

The format is specified in `docs/FORMAT.md`, a self-contained document that
arrives with the code. Until then, this repository holds no specification.

## Building and running

The build arrives with the first package of code: CMake and Ninja, C++20,
`cmake --preset <name>` to configure out of the tree into `build/<name>/`,
`cmake --build --preset <name>` to build and `ctest --preset <name>` to test.
This section will name the presets and the pinned toolchain when they exist.

## What this repository never holds

- Anyone's cognition: a live store stays at the location its user chooses,
  and a backup goes to a private repository.
- Local evidence, or a path of any machine.
- A real secret: the secret check's fixtures use documented example values,
  or build their strings when a test runs.

## License

Apache License 2.0; see `LICENSE`.
