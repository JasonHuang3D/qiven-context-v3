# tests

layer products, status built

- **Holds:** the product's whole-program tests: the drives, the contract, machine and slow tests, the child processes, and the fuzz targets
- **Never holds:** a module's unit tests (each module's own)
- **Contract:** `tests/driver/drive.hpp`; not yet in this directory: its files are the `present` paths of module.json.
- **Consumers:** app, structure
- **Consumers rely on:** the drives' requests and replies, a canonical JSON value a line
- **Consumers give:** none
- **Refuses:** a request it cannot read, as the error kind `request`
