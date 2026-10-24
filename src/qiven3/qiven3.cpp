#include "qiven3/qiven3.hpp"

namespace qiven3 {
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

}  // namespace qiven3
