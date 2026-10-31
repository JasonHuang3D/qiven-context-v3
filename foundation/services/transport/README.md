# transport

layer services, status built

- **Holds:** the wire: a request (arguments as strict UTF-8, content as bytes) and its answer (bytes and an exit status), read within the bounds its caller gives; its channel today one process per call
- **Never holds:** a verb, a store, an entry or a scope
- **Contract:** `include/qiven/transport/wire.hpp`; not yet in this directory: its files are the `present` paths of module.json.
- **Consumers:** location, fuzzing, app, driver
- **Consumers rely on:** a request read whole within its bounds, or the wire's own failure; an answer written whole
- **Consumers give:** the bounds: how many arguments, the bytes of one, the bytes of all
- **Refuses:** a request it cannot read or past a bound, given in the channel's own form
- **Words:**
  - `channel`: how a request reaches the wire: a process per call; a pipe or a socket an adapter slot of this module
