#include "qiven3/utf.hpp"

#include <array>

namespace qiven3 {

std::string_view name(ParseCode code) noexcept {
    static constexpr std::array<std::string_view, 13> names = {
        "bom",           "utf8", "depth", "syntax",    "float",   "constant",      "int_range",
        "duplicate_key", "null", "surrogate", "nonchar", "key_syntax", "not_canonical",
    };
    return names.at(static_cast<std::size_t>(code));
}

}  // namespace qiven3
