# matcher

layer core, status built

- **Awaiting the analysis:** its headers' units for the analysis, `tests/support/headers/matcher_basic.cpp` and `tests/support/headers/matcher_read.cpp` (over `qiven/matcher/basic.hpp` and `qiven/matcher/read.hpp`, paths of structure's row), stay under `tests/support/headers/` until the analysis is taken up after the foundation's reconstruction; their place is settled then.
- **Holds:** the target `qiven_matcher`: a linear matcher of a regular expression subset: `compile` into a position automaton, `hit` on a string, and their instantiation over memory's allocator; its own tests (`tests/`), over patterns it writes itself: the engine's vectors and compile's faults, the linear read, compile's budget and its fault run
- **Never holds:** the patterns of any caller, nor the cases over them; a system call or I/O
- **Contract:** `foundation/core/matcher/include/qiven/matcher/matcher.hpp`
- **Files:** `include/qiven/matcher/` (`matcher.hpp`, `basic.hpp`, `read.hpp`), `src/` (`matcher.cpp`), `tests/`, `CMakeLists.txt`; their namespace `qiven::matcher`
- **Consumers rely on:** time linear in the string; a search anywhere or, for a pattern printed `^...$`, the whole value
- **Parameters:** none
- **Refuses:** a pattern outside its subset or past its bounds (`PatternError`: syntax, unsupported, size)
