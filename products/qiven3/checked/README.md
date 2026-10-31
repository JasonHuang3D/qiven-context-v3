# checked

layer products, status built

- **Holds:** cognition's rules over a store's write, as the admit hook, and the ruled stores
- **Never holds:** a store's own guards (the record log's)
- **Contract:** `src/checked/stores.hpp`, `src/checked/rules.hpp`; not yet in this directory: its files are the `present` paths of module.json.
- **Consumers:** driver, structure
- **Consumers rely on:** every append, forget and import of a ruled store passing the rules; a call given another hook does not compile
- **Consumers give:** none
- **Refuses:** a write the rules refuse
