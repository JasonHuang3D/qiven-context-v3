#pragma once

// projectmap: the map's rules and its mark; the map files' parser is
// projectmap_files.hpp's. A map's fields, its scope, its basis, its object's
// keys and digest and its contract or constraints are content's (an entry
// reference carries a digest in a map's object alone: content takes it
// there, and refs refuses it, unknown_field, anywhere else, a map's source
// included); an entry reference not held, a revision's identity and a map
// in evidence are validate's (reference, identity, map_evidence).
// check_map adds the one rule left, which needs the view: a map's object
// of type entry names a record's root, an entry of a non-change kind with
// no target. A held entry that is not one is MapError reference; an id
// the view does not hold is validate's, and passes here. The view counts
// the call's earlier entries as held, as validate's does.
//
// mark computes a map version's mark from its object's bytes, as the map
// check gives them: the HMAC under the version's key over the bytes, equal
// to the object's digest fresh, else stale; MISSING (no blob or tree at the
// commit, or a record not active) stale; none (std::nullopt: a repository
// the registry does not name, a checkout or ref that cannot be read, a
// reader that fails) unchecked. An entry not a map is MapError kind, and a map
// content with no object digest MapError object; MapError at is
// marks_bytes' (projectmap_files.hpp).
//
// Readings: an entry is its header and its content's value, as served's;
// check_map's fault reads reference, the code of an entry reference's
// other faults, since the text names none for it.
//
// Allocations: check_map none of its own; the view's read of the named
// header, as the view states, freed before it returns. mark one, the
// digest's text, 76 bytes on the caller's allocator, freed before it
// returns; with MISSING or none, none. Platform calls: the key's wipe of
// its padded block. try_check_map is constexpr and a template on the
// allocators, so its vectors run in constant evaluation too; mark is not
// (digest::keyed reads the key). Each reads by index, with no pointer
// arithmetic. projectmap logs nothing.
//
// thread: none; the view's, and a key's (its platform's).

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "entry/header.hpp"
#include "entry/kinds.hpp"
#include "entry/served.hpp"
#include "platform/platform.hpp"
#include "qiven3/canon.hpp"
#include "qiven3/digest.hpp"

namespace qiven3::entry::projectmap {

// The rules' and the mark's codes.
enum class Code : std::uint8_t { kind, reference, object, at };
inline constexpr std::array<std::string_view, 4> code_names = {"kind", "reference", "object", "at"};
constexpr std::string_view name(Code code) noexcept { return code_names[static_cast<std::size_t>(code)]; }

struct MapError {
    Code code;
};
static_assert(sizeof(MapError) == 1, "MapError's size is pinned: its code");

// An object no commit holds, or a record not active.
struct Missing {};
inline constexpr Missing MISSING{};

// The view check_map reads: a held header by id, into the caller's
// buffer, false when not held. validate's View satisfies it.
template <typename V, typename A>
concept HeaderView = requires(V& v, std::string_view id, header::Header<A>& out) {
    { v.header(id, out) } -> std::same_as<bool>;
};

namespace detail {

using header::Slot;

// The string member key of object at, or none.
template <typename B>
constexpr std::optional<std::string_view> string_member(const canon::Basic<B>& v, json::Index at,
                                                        std::string_view key) {
    if (v.kind(at) != json::Kind::object) {
        return std::nullopt;
    }
    const std::optional<json::Index> m = v.find(at, key);
    if (!m || v.kind(*m) != json::Kind::string) {
        return std::nullopt;
    }
    return v.text(*m);
}

// The content's object, or none.
template <typename B>
constexpr std::optional<json::Index> object(const canon::Basic<B>& content) {
    if (content.empty() || content.kind(0) != json::Kind::object) {
        return std::nullopt;
    }
    return content.find(0, "object");
}

// Whether a header is a record's root: a non-change kind with no target.
template <typename A>
constexpr bool root(const header::Header<A>& h) {
    const std::string_view kind = h.value(Slot::kind);
    const bool change =
        std::find(kinds::CHANGE_KINDS.begin(), kinds::CHANGE_KINDS.end(), kind) != kinds::CHANGE_KINDS.end();
    return !change && !h.present(Slot::target);
}

// The map's object digest: MapError kind for another kind, object for a
// content with none.
template <typename A, typename B>
constexpr std::string_view object_digest(const header::Header<A>& h, const canon::Basic<B>& content) {
    if (h.value(Slot::kind) != "map") {
        throw MapError{Code::kind};
    }
    const std::optional<json::Index> o = object(content);
    const std::optional<std::string_view> d = o ? string_member(content, *o, "digest") : std::nullopt;
    if (!d) {
        throw MapError{Code::object};
    }
    return *d;
}

}  // namespace detail

// The map's rule against the view: a map whose object of type entry names
// a held entry that is not a record's root is reference; any other entry,
// or an id not held, passes.
template <typename A, typename B, typename V>
    requires HeaderView<V, A>
constexpr std::optional<Code> try_check_map(const header::Header<A>& h, const canon::Basic<B>& content, V& view) {
    if (h.value(header::Slot::kind) != "map") {
        return std::nullopt;
    }
    const std::optional<json::Index> o = detail::object(content);
    if (!o || detail::string_member(content, *o, "type") != std::optional<std::string_view>("entry")) {
        return std::nullopt;
    }
    const std::optional<std::string_view> id = detail::string_member(content, *o, "id");
    if (!id) {
        return std::nullopt;
    }
    header::Header<A> named(h.get_allocator());
    if (!view.header(*id, named)) {
        return std::nullopt;
    }
    return detail::root(named) ? std::nullopt : std::optional<Code>(Code::reference);
}

// try_check_map, a fault thrown as MapError.
template <typename A, typename B, typename V>
    requires HeaderView<V, A>
constexpr void check_map(const header::Header<A>& h, const canon::Basic<B>& content, V& view) {
    if (const std::optional<Code> fault = try_check_map(h, content, view)) {
        throw MapError{*fault};
    }
}

// A map version's mark from its object's bytes: fresh when the keyed digest
// of the bytes under the version's key is the object's, else stale. The
// digest's text is written on allocator C, one allocation.
template <platform::Platform P, typename A, typename B, typename C = std::allocator<char>>
served::Mark mark(const header::Header<A>& h, const canon::Basic<B>& content, const digest::Key<P>& key,
                  std::string_view object_bytes, const C& allocator = C()) {
    const std::string_view held = detail::object_digest(h, content);
    std::basic_string<char, std::char_traits<char>, C> computed(allocator);
    digest::keyed(key, object_bytes, computed);
    return digest::same(computed, held) ? served::Mark::fresh : served::Mark::stale;
}

// The object missing: stale.
template <platform::Platform P, typename A, typename B>
served::Mark mark(const header::Header<A>& h, const canon::Basic<B>& content, const digest::Key<P>& /*key*/,
                  Missing /*object*/) {
    static_cast<void>(detail::object_digest(h, content));
    return served::Mark::stale;
}

// No bytes read: unchecked.
template <platform::Platform P, typename A, typename B>
served::Mark mark(const header::Header<A>& h, const canon::Basic<B>& content, const digest::Key<P>& /*key*/,
                  std::nullopt_t /*object*/) {
    static_cast<void>(detail::object_digest(h, content));
    return served::Mark::unchecked;
}

}  // namespace qiven3::entry::projectmap
