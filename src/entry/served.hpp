#pragma once

// served: an entry's served block, its stub, its cost, and the budgets.
// block writes a version's block, UTF-8 lines each ending in LF, then the
// empty line:
// 1. <kind> <name> [<band>] <short id>: claim for a reflection writer's
//    lesson until its label lifts it; the name the subject, a map's
//    object (repo:<repository>/<path>, #<name> after it when it has one,
//    or entry:<id>), or a relation's from when it has no subject; the
//    first 16 hex digits of the id;
// 2. by: <label>, the label as labels gives it, escaped as a field's text
//    is (a path's U+FEFF in a label reads as it does on line 1);
// 3. state: <mark>, then ; map check <at> when a map's mark is given, and
//    each that applies: ; as of <date>, ; when <time> (<quality>),
//    ; expires <time>, ; forked +<n>, ; duplicate +<n>; and last, on an
//    item, ; item <state>, its state field's value;
// 4. the kind's own fields present, in kinds' order, but a map's object,
//    a direction's clears, a checkpoint's detail and an item's state, each
//    by its form, its text escaped onto one line (served_text.hpp). The
//    common fields are never among them.
//
// The mark: a map version's is the marks file's, fresh or stale with its
// map check's at, which the caller reads (projectmap's parser) and gives
// as a MapMark; none given reads unchecked. Another kind's is none, and a
// MapMark given for it is ServedError mark. The state is the fold's, with
// the count of the other records of its identity the fold serves, which
// the caller's selection gives (duplicate). A kind that is not served is
// ServedError kind; a state the fold does not serve, ServedError state; a
// served kind's content without its band, its name, an item's state or a
// field's shape, ServedError content. The content is the admitted, canonical one, so its
// members are read in code point order as they stand.
//
// stub writes lines 1 to 3, then stub: <cost> units; read by id <id>, the
// block's cost and full id, and the empty line: a page serves it for a
// block over block_max, the smallest band. cost is a text's UTF-16 units,
// a character past the BMP counting two. BUDGETS are the bands' and the
// page's, in tokens and in UTF-16 units.
//
// Allocations: block one, the block's text; stub one, the stub's; each
// measures its bytes first, with no allocation, then writes them into the
// caller's string, replaced only on success. A size past json::max_input
// is MemoryError budget before it; every size's sum is checked. cost,
// map_mark and the accessors none. Each is constexpr and a template on
// the allocator or the string type, so its vectors run in constant
// evaluation too; it reads by index, with no pointer arithmetic, and
// writes deeper values with no recursion. served logs nothing.
//
// thread: none

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "entry/fold.hpp"
#include "entry/header.hpp"
#include "entry/kinds.hpp"
#include "entry/labels.hpp"
#include "entry/served_text.hpp"
#include "memory/memory.hpp"
#include "qiven3/canon.hpp"
#include "qiven3/utc.hpp"

namespace qiven3::entry::served {

enum class Code : std::uint8_t { kind, state, mark, content };
inline constexpr std::array<std::string_view, 4> code_names = {"kind", "state", "mark", "content"};
constexpr std::string_view name(Code code) noexcept { return code_names[static_cast<std::size_t>(code)]; }

struct ServedError {
    Code code;
};
static_assert(sizeof(ServedError) == 1, "ServedError's size is pinned: its code");

// The marks: fresh and stale a map check's; unchecked a map's with none;
// none another kind's.
enum class Mark : std::uint8_t { none, unchecked, fresh, stale };
inline constexpr std::array<std::string_view, 4> mark_names = {"none", "unchecked", "fresh", "stale"};
constexpr std::string_view name(Mark mark) noexcept { return mark_names[static_cast<std::size_t>(mark)]; }

// A map version's mark from the marks file, with its map check's at, held
// by value.
struct MapMark {
    Mark mark = Mark::unchecked;
    std::array<char, utc::at_size> at{};

    [[nodiscard]] constexpr std::string_view when() const noexcept { return {at.data(), at.size()}; }
};
static_assert(sizeof(MapMark) == 25, "MapMark's size is pinned: its mark and its at");

// A MapMark of fresh or stale and an at in its syntax; none otherwise.
constexpr std::optional<MapMark> map_mark(Mark mark, std::string_view at) noexcept {
    if ((mark != Mark::fresh && mark != Mark::stale) || utc::check_at(at).has_value()) {
        return std::nullopt;
    }
    MapMark m;
    m.mark = mark;
    for (std::size_t i = 0; i < m.at.size(); ++i) {
        m.at[i] = at[i];
    }
    return m;
}

// A record's state as served: the fold's, and the count of the other
// records of its identity the fold serves.
struct State {
    fold::State fold;
    std::uint32_t duplicates = 0;
};
static_assert(sizeof(State) == 20, "State's size is pinned: the fold's state and the duplicates");

// The budgets: each band's, the page's own lines' and a page's, in tokens
// (the owner's measure, enforced nowhere) and in UTF-16 units at 3 per
// token, which bind.
enum class Band : std::uint8_t { core, high, normal, low, page_lines, page };
inline constexpr std::array<std::string_view, 6> band_names = {"core",       "high", "normal",
                                                               "low",        "the page's own lines",
                                                               "a page"};
struct Budget {
    Band band = Band::core;
    std::uint32_t tokens = 0;
    std::uint32_t units = 0;
};
static_assert(sizeof(Budget) == 12, "Budget's size is pinned: its band, padded, and two counts");
inline constexpr std::array<Budget, 6> BUDGETS = {{{Band::core, 2000, 6000},
                                                   {Band::high, 2750, 8250},
                                                   {Band::normal, 2750, 8250},
                                                   {Band::low, 2000, 6000},
                                                   {Band::page_lines, 500, 1500},
                                                   {Band::page, 10000, 30000}}};
// A block costs at most the smallest band's units; a page serves a longer
// one as its stub.
inline constexpr std::size_t block_max = 6000;
static_assert(block_max == BUDGETS[0].units && block_max == BUDGETS[3].units);

// The first hex digits of an id that a block's first line shows.
inline constexpr std::size_t short_id = 16;

namespace detail {

[[noreturn]] inline void refuse(Code code) { throw ServedError{code}; }

// A member's string, or empty when absent; a member of another type is
// content.
template <typename B>
constexpr std::string_view text_of(const canon::Basic<B>& v, json::Index object, std::string_view key) {
    const std::optional<json::Index> i = v.find(object, key);
    if (!i) {
        return {};
    }
    if (v.kind(*i) != json::Kind::string) {
        refuse(Code::content);
    }
    return v.text(*i);
}
// An object member's index, or none; a member of another type is content.
template <typename B>
constexpr std::optional<json::Index> object_of(const canon::Basic<B>& v, std::string_view key) {
    const std::optional<json::Index> i = v.find(0, key);
    if (i && v.kind(*i) != json::Kind::object) {
        refuse(Code::content);
    }
    return i;
}

// What lines 1 to 3 read, each part found and checked before a byte is
// written; an absent part is empty. The name is its prefix, then its
// first part, then /<path> and #<anchor> when present.
struct LinesView {
    std::string_view kind;
    std::string_view prefix;
    std::string_view first;
    std::string_view path;
    std::string_view anchor;
    std::string_view band;
    std::string_view id;
    std::string_view label;
    std::string_view as_of;
    std::string_view time;
    std::string_view quality;
    std::string_view expires;
    std::string_view item_state;
    std::size_t kind_index = 0;
    Mark mark = Mark::none;
    std::optional<MapMark> map;
    std::uint32_t forks = 0;
    std::uint32_t duplicates = 0;
};

template <typename A, typename B>
constexpr LinesView lines(const header::Header<A>& h, const canon::Basic<B>& v, const State& state,
                          const labels::Label& label, const std::optional<MapMark>& mark) {
    LinesView l;
    l.kind = h.value(header::Slot::kind);
    const std::optional<std::size_t> k = kinds::index_of(l.kind);
    if (!k || !kinds::KINDS.at(*k).served) {
        refuse(Code::kind);
    }
    if (!state.fold.served()) {
        refuse(Code::state);
    }
    const bool map = l.kind == "map";
    if (mark && !map) {
        refuse(Code::mark);
    }
    if (v.empty() || v.kind(0) != json::Kind::object) {
        refuse(Code::content);
    }
    l.kind_index = *k;
    if (l.kind == "lesson" && header::role(h.value(header::Slot::writer)) == header::reflect_role && !label.lifted()) {
        l.kind = "claim";
    }
    if (map) {
        const json::Index o = object_of(v, "object").value_or(0);
        const std::string_view type = o == 0 ? std::string_view{} : text_of(v, o, "type");
        if (type == "repo") {
            l.prefix = "repo:";
            l.first = text_of(v, o, "repository");
            l.path = text_of(v, o, "path");
            l.anchor = text_of(v, o, "name");
        } else if (type == "entry") {
            l.prefix = "entry:";
            l.first = text_of(v, o, "id");
        }
        if (l.first.empty() || (type == "repo" && l.path.empty())) {
            refuse(Code::content);
        }
        l.mark = mark ? mark->mark : Mark::unchecked;
        l.map = mark;
    } else {
        l.first = text_of(v, 0, "subject");
        if (l.first.empty() && l.kind == "relation") {
            l.first = text_of(v, 0, "from");
        }
    }
    l.band = text_of(v, 0, "band");
    l.id = h.value(header::Slot::id);
    if (l.first.empty() || l.band.empty() || l.id.size() < 7 + short_id) {
        refuse(Code::content);
    }
    l.label = label.text();
    l.as_of = text_of(v, 0, "as_of");
    if (const std::optional<json::Index> when = object_of(v, "when")) {
        l.time = text_of(v, *when, "time");
        l.quality = text_of(v, *when, "quality");
        if (l.time.empty() || l.quality.empty()) {
            refuse(Code::content);
        }
    }
    l.expires = text_of(v, 0, "expires");
    if (l.kind == "item") {
        l.item_state = text_of(v, 0, "state");
        if (l.item_state.empty()) {
            refuse(Code::content);
        }
    }
    l.forks = state.fold.forks;
    l.duplicates = state.duplicates;
    return l;
}

// "; <what> <text>" when the text is present.
template <typename K>
constexpr void put_part(K& k, std::string_view what, std::string_view text) {
    if (!text.empty()) {
        k.append(what);
        put_text(k, text);
    }
}
template <typename K>
constexpr void put_count(K& k, std::string_view what, std::uint32_t n) {
    if (n > 0) {
        k.append(what);
        json::detail::put_integer(k, n);
    }
}

// Lines 1 to 3.
template <typename K>
constexpr void put_lines(K& k, const LinesView& l) {
    k.append(l.kind);
    k.push_back(' ');
    k.append(l.prefix);
    put_text(k, l.first);
    put_part(k, "/", l.path);
    put_part(k, "#", l.anchor);
    k.append(" [");
    put_text(k, l.band);
    k.append("] ");
    k.append(l.id.substr(7, short_id));
    k.append("\nby: ");
    put_text(k, l.label);
    k.append("\nstate: ");
    k.append(name(l.mark));
    if (l.map) {
        k.append("; map check ");
        k.append(l.map->when());
    }
    put_part(k, "; as of ", l.as_of);
    if (!l.time.empty()) {
        put_part(k, "; when ", l.time);
        put_part(k, " (", l.quality);
        k.push_back(')');
    }
    put_part(k, "; expires ", l.expires);
    put_count(k, "; forked +", l.forks);
    put_count(k, "; duplicate +", l.duplicates);
    put_part(k, "; item ", l.item_state);
    k.push_back('\n');
}

// Whether field is shown among a block's fields: a map's object (line 1),
// a direction's clears, a checkpoint's detail and an item's state (line 3)
// are not.
constexpr bool shown(const LinesView& l, std::string_view field) noexcept {
    return field != "object" && field != "clears" && field != "detail" && !(field == "state" && l.kind == "item");
}

// The block, lines 1 to 3 and the kind's own fields, then the empty line.
template <typename K, typename B>
constexpr void put_block(K& k, const LinesView& l, const canon::Basic<B>& v) {
    put_lines(k, l);
    for (const kinds::Field& f : kinds::fields(kinds::KINDS.at(l.kind_index))) {
        const std::string_view field = f.name.view();
        if (!shown(l, field)) {
            continue;
        }
        if (const std::optional<json::Index> i = v.find(0, field)) {
            put_field(k, field, v, *i);
        }
    }
    k.push_back('\n');
}

// The stub: lines 1 to 3, the block's cost and full id, the empty line.
template <typename K>
constexpr void put_stub(K& k, const LinesView& l, std::size_t units) {
    put_lines(k, l);
    k.append("stub: ");
    json::detail::put_integer(k, static_cast<std::int64_t>(units));
    k.append(" units; read by id ");
    k.append(l.id);
    k.append("\n\n");
}

// Measured, then written into a string of out's allocator in one
// allocation, out replaced only on success.
template <typename S, typename Put>
constexpr void emit(S& out, const Put& put) {
    Measure m;
    put(m);
    S text(out.get_allocator());
    text.reserve(m.bytes());
    Write<S> w(text);
    put(w);
    out = std::move(text);
}

}  // namespace detail

// The block of a served version: its header and admitted content, its
// record's state, its label and, for a map, its mark; into out.
template <typename A, typename B, typename S>
constexpr void block(const header::Header<A>& h, const canon::Basic<B>& content, const State& state,
                     const labels::Label& label, const std::optional<MapMark>& mark, S& out) {
    const detail::LinesView l = detail::lines(h, content, state, label, mark);
    detail::emit(out, [&](auto& k) { detail::put_block(k, l, content); });
}

// The block's cost, its empty line counted, with no allocation.
template <typename A, typename B>
constexpr std::size_t block_cost(const header::Header<A>& h, const canon::Basic<B>& content, const State& state,
                                 const labels::Label& label, const std::optional<MapMark>& mark) {
    const detail::LinesView l = detail::lines(h, content, state, label, mark);
    detail::Measure m;
    detail::put_block(m, l, content);
    return m.units();
}

// The stub of a version's block, as a page serves a block over block_max;
// into out.
template <typename A, typename B, typename S>
constexpr void stub(const header::Header<A>& h, const canon::Basic<B>& content, const State& state,
                    const labels::Label& label, const std::optional<MapMark>& mark, S& out) {
    const detail::LinesView l = detail::lines(h, content, state, label, mark);
    detail::Measure whole;
    detail::put_block(whole, l, content);
    detail::emit(out, [&](auto& k) { detail::put_stub(k, l, whole.units()); });
}

}  // namespace qiven3::entry::served
