# digest

layer core, status built

- **Holds:** SHA-256, HMAC-SHA-256, `plain` and `keyed` digests, `same`, and keys (`Key`, `new_key`, `key_hex`, `key_from_hex`, `key_text`)
- **Never holds:** a format's framing or id rule; a system call or I/O
- **Contract:** `include/qiven/digest/digest.hpp`; not yet in this directory: its files are the `from` row's.
- **Consumers:** the `base` row's: log, wire, store, entry, secret, checked, location, write, fuzzing
- **Consumers rely on:** SHA-256 and HMAC-SHA-256 as their standards state; digests written `sha256:` or `hmac-sha256:` and lowercase hex
- **Consumers give:** none
- **Refuses:** a key text that `key_from_hex` cannot read
