# digest

layer core, status built

- **Holds:** the target `qiven_digest`, over memory alone: SHA-256 (`Sha256`, `sha256`), HMAC-SHA-256 (`hmac`, over a key of any length, and `detail::hmac_block`, over a padded key's block), `write_hex`, the `plain` digest's text, the `keyed` digest's text over a 32-byte key's bytes with the caller's wipe of the padded key's block, and `same`, a comparison whose time does not depend on where two texts differ; the plain digest's instantiation over memory's string; its own tests (`tests/`), over data and keys they write themselves: SHA-256's and RFC 4231's vectors, `keyed`'s text and its one wipe of the padded block, `same`'s, `plain`'s budget and fault run, and a subset of the vectors evaluated by the compiler, in `qiven_digest_test`
- **Never holds:** a platform's header or call, a key type, I/O, or a format's or product's concept
- **Contract:** `foundation/core/digest/include/qiven/digest/digest.hpp`
- **Files:** `include/qiven/digest/` (`digest.hpp`), `src/` (`digest.cpp`), `tests/`, `CMakeLists.txt`; their namespace `qiven::digest`
- **Consumers rely on:** SHA-256 and HMAC-SHA-256 as their standards state; digests written `sha256:` or `hmac-sha256:` and 64 lowercase hex digits, into the caller's string, replaced only on success: one allocation, the result's; `keyed`'s padded key's block wiped by the caller's wipe before the text is written; the functions but `keyed` and `same` constexpr
- **Parameters:** none
- **Refuses:** nothing: every input has its digest; an allocation refused is memory's error, the caller's string unchanged
