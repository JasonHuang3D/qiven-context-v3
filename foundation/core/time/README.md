# time

layer core, status built

- **Holds:** the target `qiven_time`, a header alone over the standard library: `utc`: a time as milliseconds since 1970 UTC (`Millis`), read from and printed as its `at`, and a date, read; its own tests (`tests/`), over texts and times they write themselves: utc's vectors, evaluated by the compiler and again at run time, the zone the C runtime reads, `print_at`'s budget and its fault run, in `qiven_time_test`, and `test_utc.py`, which runs those cases under two zones
- **Never holds:** a time zone, the system clock, or a product's concept
- **Contract:** `foundation/core/time/include/qiven/time/utc.hpp`
- **Files:** `include/qiven/time/` (`utc.hpp`), `tests/`, `CMakeLists.txt`; their namespace `qiven::time`, with `qiven::time::utc`
- **Consumers rely on:** pure, constexpr functions; no allocation but `print_at`'s result; no reading of the process's zone
- **Parameters:** none
- **Refuses:** any other form, a leap second, a year 0, a date that does not exist; in printing, a time outside year 1 to year 9999
- **Words:**
  - `at`: a time's text, `YYYY-MM-DDTHH:MM:SS.mmmZ`, 24 ASCII characters, UTC
  - `date`: the first 10 characters of an `at`, `YYYY-MM-DD`, alone
