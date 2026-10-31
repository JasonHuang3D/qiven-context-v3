# digest

layer core, status built

- **Awaiting a split:** the format's framing and id (`each_framed`, `frame`, `entry_id`) and `Key` (wiped by the platform, drawn from its random bytes, so including the platform's contract) leave `src/qiven3/digest.hpp` for `products/qiven3/format`
- **Holds:** SHA-256, HMAC-SHA-256, `plain` and `keyed` digests, `same`, and keys (`Key`, `new_key`, `key_hex`, `key_from_hex`, `key_text`)
- **Never holds:** I/O; a system call of its own (`Key` calls the platform through its contract, until it leaves)
- **Contract:** `src/qiven3/digest.hpp`
- **Files:** not yet in this directory: its files are the `from` row's; its contract's header moves to `include/qiven/digest/digest.hpp`.
- **Consumers rely on:** SHA-256 and HMAC-SHA-256 as their standards state; digests written `sha256:` or `hmac-sha256:` and lowercase hex
- **Parameters:** none
- **Refuses:** a key text that `key_from_hex` cannot read
