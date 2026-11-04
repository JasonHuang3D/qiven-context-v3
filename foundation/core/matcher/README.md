# matcher

layer core, status built

- **Holds:** the target `qiven_matcher`: a linear matcher of a regular expression subset: `compile` into a position automaton, `hit` on a string, and their instantiation over memory's allocator; its own tests (`tests/`), over patterns it writes itself: the engine's vectors and compile's faults, the linear read, compile's budget and its fault run
- **Never holds:** the patterns of any caller, nor the cases over them; a system call or I/O
- **Contract:** `foundation/core/matcher/include/qiven/matcher/matcher.hpp`
- **Files:** `include/qiven/matcher/` (`matcher.hpp`, `basic.hpp`, `read.hpp`), `src/` (`matcher.cpp`), `tests/`, `CMakeLists.txt`; their namespace `qiven::matcher`
- **Consumers rely on:** time linear in the string; a search anywhere or, for a pattern printed `^...$`, the whole value
- **Parameters:** none
- **Refuses:** a pattern outside its subset or past its bounds (`PatternError`: syntax, unsupported, size)
