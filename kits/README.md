# kits

layer kits, status group

Code shared by the products of one domain, free of any one product. A kit may name its domain's concepts.

- Depends on the foundation and, by a declared edge, on another kit; never on a product. An edge goes down only; within a layer a module depends on another only by a declared edge; the graph has no cycle.
- No system header and no conditional on the system. Code names a contract, never an adapter.

Each subdirectory holding a module.json is one module or slot; its README.md is its charter.
