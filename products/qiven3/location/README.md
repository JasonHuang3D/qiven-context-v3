# location

layer products, status built

- **Holds:** the human's choice of the store's directory: `init`, `change`, `show`, and the map check
- **Never holds:** a default path: no path is chosen for the human
- **Contract:** `src/qiven3/location.hpp`; not yet in this directory: its files are the `present` paths of module.json.
- **Consumers:** app, driver, child, structure
- **Consumers rely on:** the choice recorded in the configuration directory under its lock; the drive's facts read now
- **Consumers give:** none
- **Refuses:** a directory its checks refuse, unless accepted where the command takes `--accept`
