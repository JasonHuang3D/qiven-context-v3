# matcher

layer core, status built

- **Holds:** a linear matcher of a regular expression subset: `compile` into a position automaton, `hit` on a string
- **Never holds:** the patterns of any caller; a system call or I/O
- **Contract:** `src/entry/matcher.hpp`
- **Files:** not yet in this directory: its files are the `present` paths of module.json; its contract's header moves to `include/qiven/matcher/matcher.hpp`.
- **Consumers rely on:** time linear in the string; a search anywhere or, for a pattern printed `^...$`, the whole value
- **Parameters:** none
- **Refuses:** a pattern outside its subset or past its bounds (`PatternError`: syntax, unsupported, size)
