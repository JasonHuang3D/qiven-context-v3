# app

layer products, status built

- **Holds:** the `qiven3` binary: its subcommands, its adapters and its exit status
- **Never holds:** a module's logic beyond choosing the adapters
- **Contract:** `src/app/main.hpp`; not yet in this directory: its files are the `present` paths of module.json.
- **Consumers:** structure
- **Consumers rely on:** a command line read through the wire within the binary's bounds; one exit status per outcome
- **Consumers give:** none
- **Refuses:** a request the wire cannot read: exit 2, no line
