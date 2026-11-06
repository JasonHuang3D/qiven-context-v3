// format's compiled parts: a marker's version, the framing's size error,
// and the framing's and the id's instantiation for v3's string, over an
// arena, compiled once here. A Key is a template on the platform,
// instantiated by each binary for its adapter.

#include "qiven3/format/qiven3.hpp"

#include <limits>

#include "qiven/memory/memory.hpp"
#include "qiven3/format/entry_id.hpp"

namespace qiven3::format {
namespace {

std::optional<int> number(std::string_view digits) {
    if (digits.empty() || digits.size() > 4 || (digits.size() > 1 && digits[0] == '0')) {
        return std::nullopt;
    }
    int value = 0;
    for (char c : digits) {
        if (c < '0' || c > '9') {
            return std::nullopt;
        }
        value = value * 10 + (c - '0');
    }
    return value;
}

}  // namespace

std::optional<Version> parse_version(std::string_view text) {
    const auto dot = text.find('.');
    if (dot == std::string_view::npos) {
        return std::nullopt;
    }
    const auto major = number(text.substr(0, dot));
    const auto minor = number(text.substr(dot + 1));
    if (!major || !minor) {
        return std::nullopt;
    }
    return Version{*major, *minor};
}

namespace detail {

void too_large(std::size_t held) {
    throw qiven::memory::MemoryError::budget("format", std::numeric_limits<std::size_t>::max(),
                                             std::numeric_limits<std::size_t>::max(), held);
}

}  // namespace detail

template void frame(std::span<const Field>, qiven::memory::String&);
template void entry_id(std::span<const Field>, qiven::memory::String&);

}  // namespace qiven3::format
