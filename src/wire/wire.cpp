#include "wire/wire.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

#include "memory/fail.hpp"
#include "qiven3/utf.hpp"

namespace qiven3::wire {

std::string_view RequestView::operator[](std::size_t i) const noexcept {
    if (i >= arguments_.size()) {
        memory::fail_fast("wire: an argument past the request's");
    }
    return std::string_view(arguments_[i]);
}

std::optional<RequestView> read_request(int argc, const char* const* argv, const Bounds& bounds) noexcept {
    if (argc < 0 || (argc > 0 && argv == nullptr)) {
        return std::nullopt;
    }
    if (argc == 0) {
        return RequestView{};
    }
    const std::span<const char* const> arguments =
        std::span<const char* const>(argv, static_cast<std::size_t>(argc)).subspan(1);
    if (arguments.size() > bounds.count) {
        return std::nullopt;
    }
    std::size_t total = 0;  // within bounds.total_bytes throughout
    for (const char* text : arguments) {
        if (text == nullptr) {
            return std::nullopt;
        }
        const std::string_view one(text);
        if (one.size() > bounds.argument_bytes || one.size() > bounds.total_bytes - total) {
            return std::nullopt;
        }
        if (utf::check(one).has_value()) {
            return std::nullopt;
        }
        total += one.size();
    }
    return RequestView(arguments, total);
}

}  // namespace qiven3::wire
