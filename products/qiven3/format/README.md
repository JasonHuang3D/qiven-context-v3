# format

layer products, status built

- **Holds:** the format's names and versions (`qiven3.hpp`), and the entry's framing and id (`Field`, `each_framed`, `framed_size`, `frame`, `entry_id`)
- **Never holds:** hashes or HMAC (`digest`'s)
- **Contract:** `src/qiven3/qiven3.hpp`, `src/qiven3/digest.hpp` (namespace `qiven3`); not yet in this directory: its files are the `from` row's.
- **Consumers:** the `base` row's: log, wire, store, entry, secret, checked, location, write, fuzzing
- **Consumers rely on:** a bundle marker's version `<major>.<minor>`; an entry's id: `sha256:` and 64 lowercase hex over its framed fields
- **Consumers give:** none
- **Refuses:** a version of any other form: no value
