#pragma once

// checked/ruled: a ruled store, the store S of the contract with the rules
// (checked/rules.hpp) as its admit hook, under the calls its callers make
// of a store today.
//
//   Ruled<S>   S with its constructors and every call S gives; open, where
//              S opens, gives a Ruled; header, body and scan the store's
//              bytes and keys checked (checked/reads.hpp); export_ each
//              line so; verify S's, then each record checked so
//   Scan<H>    a ruled store's scan over the store's scan H
//   Export<E>  a ruled store's export over the store's export E
//
// Ruled derives from S and adds no member and no virtual call: a call it
// does not name is S's own, and a call it names hides S's of that name.
// header(id, out) gives the line parsed and checked into out; header(id,
// line) its bytes, checked so; body(id, record) the entry's line checked
// and its record against its header, record unchanged on a refusal; a
// scan's next each record's line checked and its keys against its header,
// into a Header or as the record; an export's next each line's header
// checked so and its body key against its content. Each refusal is
// StoreCorrupt, in the words the store raised. Each check runs inside the
// store's read, on the pages the store lends it (its header, body and scan
// next given a step), and its result is copied out; an export's after the
// store's next, on the Line's arena. Allocations: S's lent reads', the
// checks' on S's lent pages or the Line's, the copy's on the caller's
// allocator (the header's, the line's, the record's or the scan's out). Platform calls and threads:
// S's, and the body's parse's.

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

#include "checked/reads.hpp"
#include "memory/memory.hpp"
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

template <class E>
class Export {
public:
    Export(E exported, std::string_view path) noexcept : e_(std::move(exported)), path_(path) {}

    store::Snapshot heads(store::Vector<store::LaneHead>& rows) { return e_.heads(rows); }
    // The next line, its line checked and its keys against its header,
    // parsed on line's arena; false at the end.
    bool next(store::Line& line) {
        if (!e_.next(line)) {
            return false;
        }
        const reads::WhereView at{store::Operation::export_, path_.view()};
        const store::KeysView keys = line.keys.view();
        const store::Header h = reads::header_of(keys.id, line.header, line.header.get_allocator(), at);
        reads::keys_match(keys, h, at);
        reads::body_key(keys, h, at);
        return true;
    }
    void close() { e_.close(); }
    // Whether it exports the state given, where E can tell: an import asks.
    template <class T>
    [[nodiscard]] bool over(const T& state) const noexcept
        requires requires(const E& e) { e.over(state); }
    {
        return e_.over(state);
    }
    // The store's export's counted peak.
    [[nodiscard]] std::size_t peak() const noexcept
        requires requires(const E& e) { e.peak(); }
    {
        return e_.peak();
    }

private:
    E e_;
    memory::Inline<store::path_bytes> path_;  // the store's, copied: an export outlives its store's moves
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
    auto export_(store::Scopes scopes, std::span<const store::AfterView> after) {
        using E = decltype(S::export_(scopes, after));
        return Export<E>(S::export_(scopes, after), where(store::Operation::export_).path);
    }
    // S's verify, then each record by a scan: its line, its keys against
    // it, part 1 again, and its body record, read after the scan's step,
    // against its header.
    store::Counts verify() {
        const store::Counts counts = S::verify();
        const reads::WhereView at = where(store::Operation::verify);
        auto scan = S::scan(store::RangeView{});
        for (std::size_t i = 0;; ++i) {
            store::Id id;
            bool body = false;
            if (!scan.next([&](const store::Record& r, const reads::Chars& chars) {
                    const store::Header h = reads::header_of(r.id.view(), r.line, chars, at);
                    reads::keys_match(r.keys(), h, at);
                    reads::body_key(r.keys(), h, at);
                    id = r.id;
                    body = r.body;
                    if (!body) {
                        reads::part1_again(S::platform(), i, r.line, std::nullopt, chars, at);
                    }
                })) {
                return counts;
            }
            if (body) {
                (void)S::body(id.view(), [&](std::string_view line, const std::string_view* held,
                                             const reads::Chars& chars) {
                    const store::Header h = reads::header_of(id.view(), line, chars, at);
                    if (held != nullptr) {
                        reads::check_body(S::platform(), id.view(), *held, h, chars, at);
                    }
                    reads::part1_again(S::platform(), i, line,
                                       held != nullptr ? std::optional<std::string_view>(*held) : std::nullopt,
                                       chars, at);
                });
            }
        }
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
