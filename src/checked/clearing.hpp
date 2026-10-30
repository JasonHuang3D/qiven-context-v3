#pragma once

// checked/clearing: a false secret hit's clearing, cognition's, over a
// store view's generic calls (ClearView).
//
// cleared(view, scope, content, pattern, at, where): whether a direction
// record of the scope clears content for pattern at at. The roots are the
// scope's direction ids with no target, in the store's order (each_keyed);
// a record's members its root and, by targets, each id of the scope that
// names a member; each member's header, its body or none and whether it
// is forgotten; the record folded (entry/fold) at at; its current
// version's clears naming content and pattern. The first record that
// clears ends the walk. A record past the entries held, a member not
// held, a direction's content the parser refuses or a record the fold
// refuses: StoreCorrupt incoherent, at where.
//
// held(view, content, allocator): whether a held line holds the content's
// field, "content":"<content>", exact on canonical lines: a string value
// holds no unescaped quote.
//
// Allocations: each record's work on its own work allocator (the view's
// work: the scratch's next arena, the last one's given back; else the
// allocator given): its members' ids, a member's targets, headers, values,
// items, a body and the fold's. held: the field's bytes on the allocator
// given. No platform call.
//
// thread: the view's.

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "entry/fold.hpp"
#include "entry/header.hpp"
#include "entry/kinds.hpp"
#include "entry/validator.hpp"
#include "memory/memory.hpp"
#include "qiven3/canon.hpp"
#include "store/contract.hpp"
#include "store/errors.hpp"
#include "store/stage.hpp"

namespace qiven3::checked::clearing {

using Chars = memory::Allocator<char>;
using store::Header;
using store::Id;
using store::Vector;

namespace detail {

// A walk's step that stops it.
struct Stop {
    constexpr bool operator()(const Id& /*id*/) const noexcept { return true; }
};

}  // namespace detail

// The view's calls the clearing reads.
template <class V>
concept ClearView = requires(V& v, std::string_view s, Vector<Id>& ids, Header& h,
                             entry::validator::Text<Chars>& t) {
    { v.each_keyed(s, s, detail::Stop{}) } -> std::same_as<bool>;
    v.targets(s, s, ids);
    { v.header(s, h) } -> std::same_as<bool>;
    { v.body(s, t) } -> std::same_as<bool>;
    { v.forgotten(s) } -> std::same_as<bool>;
    { v.contains(s) } -> std::same_as<bool>;
    { v.count() } -> std::same_as<std::uint64_t>;
};

namespace detail {

namespace fold = entry::fold;
using entry::header::Slot;

[[noreturn]] inline void incoherent(const store::stage::AtView& where, std::string_view detail) {
    throw store::StoreError::corrupt(store::name(where.op), where.path, store::Corrupt::incoherent, detail);
}

// The view's work allocator where it gives one, else allocator.
template <class V>
Chars work(V& view, const Chars& allocator) {
    if constexpr (requires {
                      { view.work() } -> std::same_as<Chars>;
                  }) {
        return view.work();
    } else {
        return allocator;
    }
}

// Whether the direction record of root clears content for pattern at at.
template <ClearView V>
bool clears(V& view, const Id& root, std::string_view scope, std::string_view content, std::string_view pattern,
            std::string_view at, const store::stage::AtView& where, const Chars& allocator) {
    const Chars w = work(view, allocator);
    Vector<Id> members(w);
    members.push_back(root);
    {
        Vector<Id> named(w);
        for (std::size_t i = 0; i < members.size(); ++i) {
            view.targets(members[i].view(), scope, named);
            for (const Id& m : named) {
                if (members.size() > view.count()) {
                    incoherent(where, "a direction record past the entries held");
                }
                members.push_back(m);
            }
        }
    }
    Vector<Header> heads(w);
    Vector<canon::Basic<Chars>> values(w);
    Vector<fold::ItemView> items(w);
    heads.reserve(members.size());
    values.reserve(members.size());
    items.reserve(members.size());
    entry::validator::Text<Chars> body(w);
    for (const Id& m : members) {
        heads.emplace_back(w);
        if (!view.header(m.view(), heads.back())) {
            incoherent(where, "a direction record's member not held");
        }
        values.emplace_back(w);
        const bool has_body = view.body(m.view(), body);
        if (has_body && heads.back().value(Slot::kind) == "direction") {
            try {
                canon::parse(std::string_view(body), values.back());
            } catch (const ParseError&) {
                incoherent(where, "a direction's content the parser refuses");
            }
        }
        items.push_back(fold::item(heads.back(), values.back(), has_body && !view.forgotten(m.view())));
    }
    fold::order(std::span<fold::ItemView>(items).subspan(1));
    fold::State state;
    try {
        state = fold::fold(std::span<const fold::ItemView>(items), at, w);
    } catch (const fold::FoldError&) {
        incoherent(where, "a direction record the fold refuses");
    }
    if (!state.served()) {
        return false;
    }
    const canon::Basic<Chars>& v = values[state.current];
    const std::optional<json::Index> c = v.empty() ? std::nullopt : v.find(0, "clears");
    return c && v.kind(*c) == json::Kind::object && entry::kinds::detail::member(v, *c, "content") == content &&
           entry::kinds::detail::member(v, *c, "pattern") == pattern;
}

}  // namespace detail

// Whether a live clearing of scope clears content for pattern at at.
template <ClearView V>
bool cleared(V& view, std::string_view scope, std::string_view content, std::string_view pattern,
             std::string_view at, const store::stage::AtView& where, const Chars& allocator) {
    return view.each_keyed(scope, "direction", [&](const Id& root) {
        return detail::clears(view, root, scope, content, pattern, at, where, allocator);
    });
}

// Whether a held line holds content as its content field.
template <ClearView V>
bool held(V& view, std::string_view content, const Chars& allocator) {
    store::String needle(allocator);
    needle.append("\"content\":\"").append(content).append("\"");
    return view.contains(needle);
}

}  // namespace qiven3::checked::clearing
