# transport

layer services, status built

- **Holds:** the wire: a request (arguments as strict UTF-8, content as bytes) and its answer (bytes and an exit status), read within the bounds its caller gives; its channel today one process per call
- **Never holds:** a verb, a store, an entry or a scope
- **Contract:** `foundation/services/transport/include/qiven/transport/wire.hpp`
- **Files:** `include/qiven/transport/` (`wire.hpp`), `src/` (`wire.cpp`), `tests/`, `CMakeLists.txt`; their namespace `qiven::transport`
- **Consumers rely on:** a request read whole within its bounds, or the wire's own failure; an answer written whole
- **Parameters:** `bounds`
- **Refuses:** a request it cannot read or past a bound, given in the channel's own form
- **Words:**
  - `channel`: how a request reaches the wire: a process per call; a pipe or a socket an adapter slot of this module
