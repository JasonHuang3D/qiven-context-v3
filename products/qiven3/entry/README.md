# entry

layer products, status built

- **Holds:** the entry: header, body, content, kinds, labels, refs, fold, the project map, `served`, and the validator
- **Never holds:** the secret check (`secret`'s); a store
- **Contract:** `src/entry/check.hpp`
- **Files:** not yet in this directory: its files are the `present` paths of module.json.
- **Consumers rely on:** each entry of a call checked in a fixed order, the first fault deciding
- **Parameters:** none
- **Refuses:** an entry its checks or validator refuse, by its typed error
