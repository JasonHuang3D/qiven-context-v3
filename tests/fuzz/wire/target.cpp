// fuzz/wire, the per-call wire's fuzz target: its first three bytes the
// bounds, the count (the byte, 255 for none), an argument's bytes (the
// byte, 255 for none) and the total (four times the byte, 255 for none);
// the rest split at each null byte into arguments after a program's name,
// an argument vector as a system gives one, read by read_request. A
// finding is any exception, a crash or a hang; thrown here, a request
// read where the oracle refuses it or refused where the oracle reads it,
// or a request read whose views are not the arguments' bytes or whose
// bytes are not their sum. The oracle is this file's own: the count, each
// argument's bytes and their sum within the bounds, and each argument
// well formed by RFC 3629's table, written apart from utf's reader.
// wire_input is the regression case's entry; LLVMFuzzerTestOneInput
// libFuzzer's, in the fuzz binary alone.

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "wire/wire.hpp"

namespace {

namespace wire = qiven3::wire;

constexpr std::size_t none = std::numeric_limits<std::size_t>::max();
constexpr std::size_t header = 3;

void finding(std::string_view what) { throw std::logic_error("fuzz/wire: " + std::string(what)); }

// A bound from its byte: 255 none, else the byte times scale.
std::size_t bound(std::uint8_t byte, std::size_t scale) {
    return byte == 255 ? none : static_cast<std::size_t>(byte) * scale;
}

// Whether text is well formed UTF-8: RFC 3629's table, each lead byte's
// first continuation in its own range, the rest 80 to BF.
bool well_formed(std::string_view text) {
    std::size_t at = 0;
    while (at < text.size()) {
        const unsigned b = static_cast<unsigned char>(text[at]);
        std::size_t need = 0;
        unsigned lo = 0x80U;
        unsigned hi = 0xBFU;
        if (b <= 0x7FU) {
            need = 0;
        } else if (b >= 0xC2U && b <= 0xDFU) {
            need = 1;
        } else if (b == 0xE0U) {
            need = 2;
            lo = 0xA0U;
        } else if (b == 0xEDU) {
            need = 2;
            hi = 0x9FU;
        } else if (b >= 0xE1U && b <= 0xEFU) {
            need = 2;
        } else if (b == 0xF0U) {
            need = 3;
            lo = 0x90U;
        } else if (b >= 0xF1U && b <= 0xF3U) {
            need = 3;
        } else if (b == 0xF4U) {
            need = 3;
            hi = 0x8FU;
        } else {
            return false;
        }
        if (text.size() - at - 1 < need) {
            return false;
        }
        for (std::size_t k = 1; k <= need; ++k) {
            const unsigned c = static_cast<unsigned char>(text[at + k]);
            if (c < lo || c > hi) {
                return false;
            }
            lo = 0x80U;
            hi = 0xBFU;
        }
        at += need + 1;
    }
    return true;
}

}  // namespace

namespace qiven3::fuzz {

int wire_input(const std::uint8_t* data, std::size_t size) {
    const std::span<const std::uint8_t> input(data, size);
    if (input.size() < header) {
        return 0;
    }
    const wire::Bounds bounds{bound(input[0], 1), bound(input[1], 1), bound(input[2], 4)};
    std::vector<std::string> arguments(1);
    for (const std::uint8_t b : input.subspan(header)) {
        if (b == 0) {
            arguments.emplace_back();
        } else {
            arguments.back() += static_cast<char>(b);
        }
    }
    if (input.size() == header) {
        arguments.clear();  // no bytes past the bounds: no argument at all
    }
    std::vector<const char*> argv = {"qiven3"};
    for (const std::string& a : arguments) {
        argv.push_back(a.c_str());
    }
    argv.push_back(nullptr);

    bool expected = arguments.size() <= bounds.count;
    std::size_t total = 0;
    for (const std::string& a : arguments) {
        expected = expected && a.size() <= bounds.argument_bytes && well_formed(a);
        total += a.size();
    }
    expected = expected && total <= bounds.total_bytes;

    const std::optional<wire::RequestView> request =
        wire::read_request(static_cast<int>(argv.size() - 1), argv.data(), bounds);
    if (request.has_value() != expected) {
        finding(expected ? "a request refused that the oracle reads" : "a request read that the oracle refuses");
    }
    if (request) {
        if (request->size() != arguments.size() || request->bytes() != total) {
            finding("a request whose count or bytes are not its arguments'");
        }
        for (std::size_t i = 0; i < arguments.size(); ++i) {
            if ((*request)[i] != arguments[i]) {
                finding("a view but its argument's bytes");
            }
        }
    }
    return 0;
}

}  // namespace qiven3::fuzz

#if defined(QIVEN3_FUZZ_BINARY)
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    return qiven3::fuzz::wire_input(data, size);
}
#endif
