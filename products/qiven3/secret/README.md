# secret

layer products, status built

- **Holds:** the secret check: the twelve pinned patterns over every string of an entry
- **Never holds:** a pattern's matching engine (`matcher`'s)
- **Contract:** `src/entry/secret.hpp`
- **Files:** not yet in this directory: its files are the `present` paths of module.json.
- **Consumers rely on:** the first hit refused as `SecretError`, carrying the pattern and the field's path, never the value
- **Parameters:** none
- **Refuses:** an entry with a string a pattern hits
