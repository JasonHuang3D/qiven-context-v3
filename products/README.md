# products

layer products, status group

Each product's own concepts and rules. A product chooses its adapters in its program alone.

- Depends on the foundation and the kits; never on another product. An edge goes down only; within a layer a module depends on another only by a declared edge; the graph has no cycle.
- No system header and no conditional on the system. Code names a contract, never an adapter, but in the product's program.

Each subdirectory holding a module.json is one module or slot; its README.md is its charter.
