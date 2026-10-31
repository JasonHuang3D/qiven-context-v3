# time

layer core, status built

- **Holds:** `utc`: a time as milliseconds since 1970 UTC, read from and printed as `YYYY-MM-DDTHH:MM:SS.mmmZ` and its date
- **Never holds:** a time zone, the system clock, or a product's concept
- **Contract:** `include/qiven/time/utc.hpp`; not yet in this directory: its files are the `from` row's.
- **Consumers:** the `base` row's: log, wire, store, entry, secret, checked, location, write, fuzzing
- **Consumers rely on:** pure, constexpr functions; no allocation but `print_at`'s result; no reading of the process's zone
- **Consumers give:** none
- **Refuses:** any other form, a leap second, a year 0, a date that does not exist
