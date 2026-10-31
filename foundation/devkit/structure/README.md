# structure

layer devkit, status built

- **Holds:** the structure method in Python: the lexer, the records, planting, the subset check, the rules for any code, and the content check; the header units
- **Never holds:** a product's own paths, lists, plants or cells as the method's code
- **Contract:** no header: `tests/structure/`'s tests, run by unittest; not yet in this directory: its files are the `present` paths of module.json.
- **Consumers:** none by the table
- **Consumers rely on:** every tracked file read against the rules; a finding names its file and rule
- **Consumers give:** none
- **Refuses:** a file that breaks a rule: its test fails
