#pragma once

// records: the store's plain records for its cases. A record is an opaque
// synthetic line (`rec:<id>` and its keys' text, which no parser reads),
// a body record of any bytes or none, and its keys, set directly and never
// parsed from the line: the store takes a record by its keys alone.
//
//   Keys, Rec           a record's owned keys; a record: line, record and
//                       keys (line and record read as a stored entry's)
//   rec(...)            a record with a body or, body false, none
//   chain(...)          a lane's chain: seq 1, 2, ..., each the parent of
//                       the next, each with a body
//   targeting(r, t)     r with a target key t, its line restated
//   forget(...)         a forget: kind `forget`, its target, no body
//   big(...)            a record whose body is n bytes
//   views, added        the records' views with their keys; their bytes
//   id_of, seq_of, lane_head
//                       a record's id and seq; a lane's head as a source
//                       gives it
//   Refusal, refused(f) what a StoreError f raised gives, or none
//
// Allocations: the standard's, the records' strings. thread: any.

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "memory/memory.hpp"
#include "store/contract.hpp"
#include "store/errors.hpp"
#include "store/keys.hpp"

namespace qiven3::test::records {

// A record's keys, owned.
struct Keys {
    std::string id, scope, lane, kind;
    std::int64_t seq = 0;
    std::optional<std::string> parent, target;
    bool body = false;
};

// A stored record: its line, its body record or none, its keys.
struct Rec {
    std::string line;
    std::optional<std::string> record;
    Keys keys;
};

// The keys' view, over k's bytes.
inline store::KeysView view(const Keys& k) {
    const auto opt = [](const std::optional<std::string>& s) {
        return s ? std::optional<std::string_view>(*s) : std::nullopt;
    };
    return store::KeysView{k.id, k.scope, k.lane, k.kind, k.seq, opt(k.parent), opt(k.target), k.body};
}

// The opaque line of k: `rec:<id>` and each key's text.
inline std::string line_of(const Keys& k) {
    std::string out = "rec:" + k.id + " scope:" + k.scope + " lane:" + k.lane + " kind:" + k.kind +
                      " seq:" + std::to_string(k.seq);
    out += k.parent ? " parent:" + *k.parent : std::string();
    out += k.target ? " target:" + *k.target : std::string();
    return out;
}

// A body record of id: bytes no parser takes.
inline std::string body_of(std::string_view id) { return "body:" + std::string(id) + " {[opaque|bytes]}"; }

inline Rec rec(std::string_view id, std::string_view scope, std::string_view lane, std::int64_t seq,
               std::optional<std::string_view> parent = std::nullopt, std::string_view kind = "note", bool body = true) {
    Rec r;
    r.keys = Keys{std::string(id), std::string(scope), std::string(lane), std::string(kind), seq, std::nullopt,
                  std::nullopt, body};
    if (parent) {
        r.keys.parent = std::string(*parent);
    }
    r.line = line_of(r.keys);
    if (body) {
        r.record = body_of(id);
    }
    return r;
}

// A lane's chain of the ids given, of kind each, seq from 1.
inline std::vector<Rec> chain(std::string_view scope, std::string_view lane, std::initializer_list<std::string_view> ids,
                              std::string_view kind = "note") {
    std::vector<Rec> out;
    for (const std::string_view id : ids) {
        const std::optional<std::string_view> parent =
            out.empty() ? std::nullopt : std::optional<std::string_view>(out.back().keys.id);
        out.push_back(rec(id, scope, lane, static_cast<std::int64_t>(out.size()) + 1, parent, kind));
    }
    return out;
}

inline Rec targeting(Rec r, std::string_view target) {
    r.keys.target = std::string(target);
    r.line = line_of(r.keys);
    return r;
}

inline Rec forget(std::string_view id, std::string_view scope, std::string_view lane, std::int64_t seq,
                  std::optional<std::string_view> parent, std::string_view target) {
    return targeting(rec(id, scope, lane, seq, parent, "forget", false), target);
}

inline Rec big(std::string_view id, std::string_view scope, std::string_view lane, std::int64_t seq,
               std::optional<std::string_view> parent, std::size_t bytes) {
    Rec r = rec(id, scope, lane, seq, parent);
    r.record = std::string(bytes, 'b');
    return r;
}

// Views of held records with their keys: a temporary list is refused, as
// its views would dangle.
std::vector<store::EntryView> views(std::vector<Rec>&& list) = delete;
inline std::vector<store::EntryView> views(const std::vector<Rec>& list) {
    std::vector<store::EntryView> out;
    for (const Rec& r : list) {
        out.push_back(store::EntryView{r.line, r.record ? std::optional<std::string_view>(*r.record) : std::nullopt,
                                       view(r.keys)});
    }
    return out;
}

// The bytes the records add: each line and record with its LF.
inline std::uint64_t added(const std::vector<Rec>& list) {
    std::uint64_t n = 0;
    for (const Rec& r : list) {
        n += r.line.size() + 1 + (r.record ? r.record->size() + 1 : 0);
    }
    return n;
}

inline const std::string& id_of(const Rec& r) { return r.keys.id; }
inline std::int64_t seq_of(const Rec& r) { return r.keys.seq; }

inline store::LaneHead lane_head(std::string_view scope, std::string_view lane, std::string_view id,
                                 std::int64_t seq) {
    store::LaneHead h;
    h.head = store::Head{store::Id::of(id), seq};
    h.scope = memory::Inline<store::scope_bytes>(scope);
    h.lane = memory::Inline<store::lane_bytes>(lane);
    return h;
}

// What a StoreError f raised gives: its kind, its code (the store's, a
// full's or the entry's), its detail and its index; none if f returned.
struct Refusal {
    store::Kind kind = store::Kind::store_error;
    std::string code;
    std::string detail;
    std::size_t index = 0;
};

template <class F>
std::optional<Refusal> refused(const F& f) {
    try {
        f();
    } catch (const store::StoreError& e) {
        Refusal r;
        r.kind = e.kind();
        if (const std::optional<store::Code> code = e.code()) {
            r.code = store::name(*code);
        } else if (const std::optional<store::Full> full = e.full()) {
            r.code = store::name(*full);
        } else {
            r.code = e.entry_code();
        }
        r.detail = e.detail();
        r.index = e.index();
        return r;
    }
    return std::nullopt;
}

}  // namespace qiven3::test::records
