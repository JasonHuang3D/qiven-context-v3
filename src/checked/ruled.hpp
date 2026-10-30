#pragma once

// checked/ruled: a ruled store, the store S of the contract with the rules
// (checked/rules.hpp) as its admit hook, under the calls its callers make
// of a store today.
//
//   Ruled<S>   S with its constructors and every call S gives; open, where
//              S opens, gives a Ruled; header, body and scan the store's
//              bytes and keys checked (checked/reads.hpp)
//   Scan<H>    a ruled store's scan over the store's scan H
//
// Ruled derives from S and adds no member and no virtual call: a call it
// does not name is S's own, and a call it names hides S's of that name.
// header(id, out) gives the line parsed and checked into out; header(id,
// line) its bytes, checked so; body(id, record) the entry's line checked
// and its record against its header, record unchanged on a refusal; a
// scan's next each record's line checked and its keys against its header,
// into a Header or as the record. Each refusal is StoreCorrupt, in the
// words the store raised. Each check runs inside the store's read, on the
// pages the store lends it (its header, body and scan next given a step),
// and its result is copied out. Allocations: S's lent reads', the checks'
// on S's lent pages, the copy's on the caller's allocator (the header's,
// the line's, the record's or the scan's out). Platform calls and threads:
// S's, and the body's parse's.

#include <string_view>
#include <utility>

#include "checked/reads.hpp"
#include "store/contract.hpp"

namespace qiven3::checked {

template <class H>
class Scan {
public:
    Scan(H scan, std::string_view path) noexcept : scan_(std::move(scan)), at_{store::Operation::scan, path} {}

    // The next record's header, parsed and checked, into out; false at the end.
    bool next(store::Header& out) {
        return scan_.next([&](const store::Record& r, const reads::Chars& chars) {
            const store::Header h = reads::header_of(r.id.view(), r.line, chars, at_);
            reads::keys_match(r.keys(), h, at_);
            reads::copy_header(h, out);
        });
    }
    // The next record, its line checked so, into out; false at the end.
    bool next(store::Record& out) {
        return scan_.next([&](const store::Record& r, const reads::Chars& chars) {
            const store::Header h = reads::header_of(r.id.view(), r.line, chars, at_);
            reads::keys_match(r.keys(), h, at_);
            reads::copy_record(r, out);
        });
    }
    // The store's scan's counted peak.
    [[nodiscard]] std::size_t peak() const noexcept { return scan_.peak(); }

private:
    H scan_;
    reads::WhereView at_;
};

template <class S>
class Ruled : public S {
public:
    using Plain = S;  // the store it rules
    using S::S;
    // The store an open or a factory gives, ruled.
    Ruled(S&& store) noexcept : S(std::move(store)) {}

    // Opens, or creates, as S::open does, ruled: its path, create and
    // read-only flags, platform and limits, then its hooks if given.
    template <class Platform, class... Hooks>
        requires requires(std::string_view path, bool flag, Platform& platform, const store::Limits& limits,
                          Hooks&&... hooks) { S::open(path, flag, flag, platform, limits, std::forward<Hooks>(hooks)...); }
    static Ruled open(std::string_view path, bool create, bool read_only, Platform& platform,
                      const store::Limits& limits = store::Limits{}, Hooks&&... hooks) {
        return Ruled(S::open(path, create, read_only, platform, limits, std::forward<Hooks>(hooks)...));
    }

    void header(std::string_view id, store::Header& out) {
        S::header(id, [&](std::string_view line, const reads::Chars& chars) {
            reads::copy_header(reads::header_of(id, line, chars, where(store::Operation::header)), out);
        });
    }
    void header(std::string_view id, store::String& line) {
        S::header(id, [&](std::string_view held, const reads::Chars& chars) {
            (void)reads::header_of(id, held, chars, where(store::Operation::header));
            line.assign(held);
        });
    }
    // false: forgotten, a forget, or a header with no body.
    bool body(std::string_view id, store::String& record) {
        const reads::WhereView at = where(store::Operation::body);
        return S::body(id, [&](std::string_view line, const std::string_view* held, const reads::Chars& chars) {
            const store::Header h = reads::header_of(id, line, chars, at);
            if (held != nullptr) {
                reads::check_body(S::platform(), id, *held, h, chars, at);
                record.assign(*held);
            }
        });
    }
    auto scan(const store::RangeView& range) {
        using H = decltype(S::scan(range));
        return Scan<H>(S::scan(range), where(store::Operation::scan).path);
    }

private:
    // The read a refusal names: the store's path where it has one.
    [[nodiscard]] reads::WhereView where(store::Operation op) const noexcept {
        if constexpr (requires(const S& s) { s.path(); }) {
            return {op, S::path()};
        } else {
            return {op, {}};
        }
    }
};

}  // namespace qiven3::checked
