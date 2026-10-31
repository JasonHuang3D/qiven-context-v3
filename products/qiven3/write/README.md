# write

layer products, status built

- **Holds:** the entries of a file read and built for the store: keys, holes filled, ids, at and seq
- **Never holds:** a store
- **Contract:** `src/write/`'s headers; not yet in this directory: its files are the `present` paths of module.json.
- **Consumers:** fuzzing, app, structure
- **Consumers rely on:** each entry in the file's order, as the store's append or import takes it
- **Consumers give:** none
- **Refuses:** a file whose entries it cannot read or build, by its typed error
