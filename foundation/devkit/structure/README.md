# structure

layer devkit, status built

- **Awaiting a split:** its product data (qiven3's paths, lists, plants and cells, in `tests/structure/`) leaves for `products/qiven3`, and with it its row's edges to qiven3's modules
- **Holds:** the structure method in Python: the lexer, the records, planting, the subset check, the rules for any code, and the content check; the header units
- **Never holds:** a product's own paths, lists, plants or cells as the method's code
- **Contract:** none
- **Interface:** no header: `tests/structure/`'s tests, run by unittest
- **Files:** not yet in this directory: its files are the `present` paths of module.json.
- **Consumers rely on:** every tracked file read against the rules; a finding names its file and rule
- **Parameters:** none
- **Refuses:** a file that breaks a rule: its test fails
