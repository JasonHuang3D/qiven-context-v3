# diag

layer system, status built

- **Holds:** the diagnostic entry every binary runs before `main`, and the crash report by system
- **Never holds:** a product's concept; a window or a dialog
- **Contract:** `include/qiven/diag/diag.hpp`; not yet in this directory: its files are the `present` paths of module.json.
- **Consumers:** support, faults, log, app, driver, child
- **Consumers rely on:** a fault ends the process with one bounded report on standard error: its cause, stack, last events, dump and status
- **Consumers give:** none
- **Refuses:** a window on a fault: none is shown
