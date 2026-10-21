// The fuzz targets' regression case: each target's function over each of
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

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size);

namespace qiven3::fuzz {
// regress_inputs.cpp's: how many kept inputs, and each one's target, name
// and bytes.
std::size_t kept_count() noexcept;
std::string_view kept_target(std::size_t i) noexcept;
std::string_view kept_name(std::size_t i) noexcept;
std::span<const std::uint8_t> kept_bytes(std::size_t i) noexcept;
}  // namespace qiven3::fuzz

QIVEN3_CASE(fuzz_regress, digest) {
    namespace fuzz = qiven3::fuzz;
    std::size_t ran = 0;
    for (std::size_t i = 0; i < fuzz::kept_count(); ++i) {
        if (fuzz::kept_target(i) != "digest") {
            continue;
        }
        const std::span<const std::uint8_t> bytes = fuzz::kept_bytes(i);
        std::cout << "fuzz/digest: " << fuzz::kept_name(i) << ", " << bytes.size() << " bytes\n";
        CHECK_EQ(LLVMFuzzerTestOneInput(bytes.data(), bytes.size()), 0);
        ++ran;
    }
    std::cout << "fuzz/digest: " << ran << " kept inputs run\n";
    CHECK(ran > 0);
}
