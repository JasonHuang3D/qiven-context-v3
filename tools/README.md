# tools

layer tools, status built

- **Holds:** the developer tooling, in Python: the edit loop (`loop.py`), navigation (`nav.py`), the
  debuggers (`debug.py`), dumps (`dumps.py`), the watch (`watch.py`), status (`status.py`), the fuzz seeds
  and the git shim; and their drive tests.
- **Never holds:** C++ that a product's program links: tooling sits outside the link graph.
- **Contract:** each tool's `--help` and its one bounded answer; its files are the `present` paths of
  module.json, its tests not yet in this directory.
- **Consumers:** the developers and models that build and test the repository.
- **Consumers give:** none
