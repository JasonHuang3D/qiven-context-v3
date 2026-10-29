#include "write/refuse.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "write/entries_reader.hpp"

namespace qiven3::write::refuse {

std::string_view name(Code code) noexcept {
    static constexpr std::array<std::string_view, 16> names = {
        "form",      "invalid",       "secret", "owner_words", "person_scope", "owner_entry",
        "slot",      "kind",          "refs_item", "cycle",    "head_conflict", "busy",
        "store_full", "not_empty",    "identity", "corrupt"};
    return names.at(static_cast<std::size_t>(code));
}

namespace detail {

void at_file(Code code, std::string_view member) { throw Refusal{code, memory::Inline<128>(member)}; }

void at_entry(Code code, const entries::File& file, std::size_t e, std::string_view member, std::string_view field,
              std::size_t item) {
    entries::detail::Path path;
    path.member("entries");
    path.index(e);
    path.member(member);
    path.member(field);
    if (item != entries::none) {
        path.index(item);
    }
    throw Refusal{code, memory::Inline<128>(path.view()), static_cast<std::uint32_t>(e),
                  memory::Inline<64>(file.text(file.entries()[e].name))};
}

namespace {

std::optional<json::Index> member(const entries::File& file, std::size_t e, std::string_view key, json::Kind kind) {
    const json::Value& v = file.value();
    const std::optional<json::Index> at = v.find(file.entries()[e].content, key);
    return at && v.kind(*at) == kind ? at : std::nullopt;
}

}  // namespace

std::optional<std::string_view> text(const entries::File& file, std::size_t e, std::string_view key) {
    const std::optional<json::Index> at = member(file, e, key, json::Kind::string);
    return at ? std::optional<std::string_view>(file.value().text(*at)) : std::nullopt;
}

std::optional<json::Index> array(const entries::File& file, std::size_t e, std::string_view key) {
    return member(file, e, key, json::Kind::array);
}

const entries::Site* site(const entries::File& file, std::size_t e, json::Index node) {
    for (const entries::Site& s : file.sites(file.entries()[e])) {
        if (s.node == node) {
            return &s;
        }
    }
    return nullptr;
}

std::optional<std::uint64_t> slot_number(std::string_view writer) noexcept {
    constexpr std::string_view mark = "/s";
    const std::size_t at = writer.find(mark);
    if (at == std::string_view::npos) {
        return std::nullopt;
    }
    const std::string_view digits = writer.substr(at + mark.size());
    if (digits.empty() || digits.size() > 9) {  // the writer's syntax: at most nine digits
        return std::nullopt;
    }
    std::uint64_t n = 0;
    for (const char c : digits) {
        if (c < '0' || c > '9') {
            return std::nullopt;
        }
        n = n * 10 + (static_cast<std::uint64_t>(c) - std::uint64_t{'0'});
    }
    return n;
}

std::optional<std::size_t> last_item(const entries::File& file, std::string_view item_id) {
    for (std::size_t j = file.entries().size(); j-- > 0;) {
        if (file.text(file.entries()[j].kind) == "item" && text(file, j, "item_id") == item_id) {
            return j;
        }
    }
    return std::nullopt;
}

}  // namespace detail

}  // namespace qiven3::write::refuse
