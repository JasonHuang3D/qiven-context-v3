# format

layer products, status built

- **Holds:** the target `qiven3_format`: the format's names and versions (`qiven3.hpp`: `ENTRY_TAG`, the bundle's format and version, `Version`, `parse_version`); the entry's framing and id (`entry_id.hpp`: `Field`, `detail::each_framed`, `framed_size`, `frame`, `entry_id`, over digest's SHA-256); and `Key` with its functions (`key.hpp`: `Key`, 32 bytes of key material wiped by its platform at its end and on its moves, `keyed` over a `Key` with its platform's wipe, `key_text`, `key_hex`, `key_from_hex`, and `new_key`, drawn from the platform's random bytes), templates on the platform's contract, its header included by `key.hpp` alone; the framing's and the id's instantiation over memory's string; its own tests (`tests/`), over fields, keys and data they write themselves: the framing's and the id's vectors, `keyed`'s text over a `Key`, two records under their own keys, the key's wipes and `new_key`'s draw on the fake platform, the budgets and fault runs, and a subset of the vectors evaluated by the compiler, in `qiven3_format_test`
- **Never holds:** hashes or HMAC (`digest`'s), a platform's adapter, or I/O
- **Contract:** `products/qiven3/format/include/qiven3/format/qiven3.hpp`
- **Files:** `include/qiven3/format/` (`qiven3.hpp`, `entry_id.hpp`, `key.hpp`), `src/` (`qiven3.cpp`), `tests/`, `CMakeLists.txt`; their namespace `qiven3::format`
- **Consumers rely on:** a bundle marker's version `<major>.<minor>`; an entry's id: `sha256:` and 64 lowercase hex over its framed fields; a `Key` with no stream operator, formatter or accessor, its bytes read by `keyed` and `key_hex` alone and wiped by its platform; a binary that makes a key links its platform's adapter
- **Parameters:** none
- **Refuses:** a version of any other form: no value; framed fields whose size passes `std::size_t`: memory's budget error; a refused draw in `new_key`: the platform's error, the partial bytes wiped; text that is not a key's given to `key_from_hex`: a broken invariant, failed fast
