// The fuzz targets' regression cases: each target's function over each of
// its kept inputs, fuzz/<target>/regress/<sha1>, without libFuzzer. The
// build compiles the inputs into regress_inputs.cpp as bytes, so the case
// reads no file. An exception out of the function, a crash or a hang fails
// it, as under libFuzzer; so does a target with no kept input.

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <string_view>

#include "runner/runner.hpp"

namespace qiven3::fuzz {
// Each target's function, its target.cpp's.
int digest_input(const std::uint8_t* data, std::size_t size);
int entries_input(const std::uint8_t* data, std::size_t size);
int entry_input(const std::uint8_t* data, std::size_t size);
int wire_input(const std::uint8_t* data, std::size_t size);

// regress_inputs.cpp's: how many kept inputs, and each one's target, name
// and bytes.
std::size_t kept_count() noexcept;
std::string_view kept_target(std::size_t i) noexcept;
std::string_view kept_name(std::size_t i) noexcept;
std::span<const std::uint8_t> kept_bytes(std::size_t i) noexcept;
}  // namespace qiven3::fuzz

namespace {

// The target's kept inputs, each through its function; how many ran.
template <typename Input>
std::size_t kept(qiven3::test::Context& qiven3_ctx, std::string_view target, const Input& input) {
    namespace fuzz = qiven3::fuzz;
    std::size_t ran = 0;
    for (std::size_t i = 0; i < fuzz::kept_count(); ++i) {
        if (fuzz::kept_target(i) != target) {
            continue;
        }
        const std::span<const std::uint8_t> bytes = fuzz::kept_bytes(i);
        std::cout << "fuzz/" << target << ": " << fuzz::kept_name(i) << ", " << bytes.size() << " bytes\n";
        CHECK_EQ(input(bytes.data(), bytes.size()), 0);
        ++ran;
    }
    std::cout << "fuzz/" << target << ": " << ran << " kept inputs run\n";
    return ran;
}

}  // namespace

QIVEN3_CASE(fuzz_regress, digest) { CHECK(kept(qiven3_ctx, "digest", qiven3::fuzz::digest_input) > 0); }

QIVEN3_CASE(fuzz_regress, entries) { CHECK(kept(qiven3_ctx, "entries", qiven3::fuzz::entries_input) > 0); }

QIVEN3_CASE(fuzz_regress, entry) { CHECK(kept(qiven3_ctx, "entry", qiven3::fuzz::entry_input) > 0); }

QIVEN3_CASE(fuzz_regress, wire) { CHECK(kept(qiven3_ctx, "wire", qiven3::fuzz::wire_input) > 0); }
