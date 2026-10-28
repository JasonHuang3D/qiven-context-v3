#pragma once

// kinds_identity: kinds' record identity, by the kinds' tables.
// identity(header, content) is a record's identity in its scope: a map's
// object (its type, then repository, path and name, or the root's id), an
// item's item_id, a clause's clause_id, a checkpoint's for_lane where its
// content holds one, else its header's lane, and a question's question_id;
// none for a question without one, and for every other kind. Its parts
// view the header's and the content's text, the content as content's
// check passed it; a part the content lacks reads empty.
//
// Allocations: none; identity reads its arguments in place and returns
// views. It is constexpr, so its vectors run in constant evaluation too;
// it reads by index, with no pointer arithmetic. It logs nothing.
//
// thread: none

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "entry/header.hpp"
#include "entry/kinds.hpp"
#include "qiven3/canon.hpp"

namespace qiven3::entry::kinds {

// A record's identity in its scope: its kind's place and up to four parts,
// views of the header's and the content's text.
struct IdentityView {
    std::uint8_t kind = 0;
    std::array<std::string_view, 4> parts{};
    constexpr bool operator==(const IdentityView&) const = default;
};
#if INTPTR_MAX == INT64_MAX
static_assert(sizeof(IdentityView) == 72, "IdentityView's size is pinned: its kind, padded, and four views");
#endif

template <typename A, typename B>
constexpr std::optional<IdentityView> identity(const header::Header<A>& h, const canon::Basic<B>& content) {
    const std::optional<std::size_t> k = index_of(h.value(header::Slot::kind));
    if (!k || KINDS[*k].identity == Holds::none) {
        return std::nullopt;
    }
    IdentityView id;
    id.kind = static_cast<std::uint8_t>(*k);
    switch (KINDS[*k].identity) {
        case Holds::lane: {
            const std::string_view for_lane = detail::member(content, 0, "for_lane");
            id.parts[0] = for_lane.empty() ? h.value(header::Slot::lane) : for_lane;
            break;
        }
        case Holds::item_id: id.parts[0] = detail::member(content, 0, "item_id"); break;
        case Holds::clause_id: id.parts[0] = detail::member(content, 0, "clause_id"); break;
        case Holds::question_id:
            id.parts[0] = detail::member(content, 0, "question_id");
            if (id.parts[0].empty()) {
                return std::nullopt;
            }
            break;
        case Holds::object: {
            if (content.empty() || content.kind(0) != json::Kind::object) {
                break;
            }
            const std::optional<json::Index> o = content.find(0, "object");
            if (!o) {
                break;
            }
            id.parts[0] = detail::member(content, *o, "type");
            if (id.parts[0] == "entry") {
                id.parts[1] = detail::member(content, *o, "id");
            } else {
                id.parts[1] = detail::member(content, *o, "repository");
                id.parts[2] = detail::member(content, *o, "path");
                id.parts[3] = detail::member(content, *o, "name");
            }
            break;
        }
        case Holds::none: break;
    }
    return id;
}

}  // namespace qiven3::entry::kinds
