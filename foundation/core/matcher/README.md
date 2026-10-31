# matcher

layer core, status built

- **Holds:** a linear matcher of a regular expression subset: `compile` into a position automaton, `hit` on a string
- **Never holds:** the patterns of any caller; a system call or I/O
- **Contract:** `include/qiven/matcher/matcher.hpp`; not yet in this directory: its files are the `present` paths of module.json.
- **Consumers:** entry, secret, write
- **Consumers rely on:** time linear in the string; a search anywhere or, for a pattern printed `^...$`, the whole value
- **Consumers give:** none
- **Refuses:** a pattern outside its subset or past its bounds (`PatternError`: syntax, unsupported, size)
