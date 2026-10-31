# time

layer core, status built

- **Holds:** `utc`: a time as milliseconds since 1970 UTC, read from and printed as `YYYY-MM-DDTHH:MM:SS.mmmZ` and its date
- **Never holds:** a time zone, the system clock, or a product's concept
- **Contract:** `src/qiven3/utc.hpp`
- **Files:** not yet in this directory: its files are the `from` row's; its contract's header moves to `include/qiven/time/utc.hpp`.
- **Consumers rely on:** pure, constexpr functions; no allocation but `print_at`'s result; no reading of the process's zone
- **Parameters:** none
- **Refuses:** any other form, a leap second, a year 0, a date that does not exist
