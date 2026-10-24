#pragma once

// check: the shared validator's part 1, stateless, before any lock.
// check_entry reads an entry's stored header line and its body record, or
// none (a forget's, or a forgotten entry's), and returns the Entry, or
// throws the first fault, the parts read in one fixed order: the strict
// parser's (the line's, then the record's) as ParseError; the header's
// rules (a line whose root is no object, then header's check: unknown_key
// to id_mismatch) as HeaderError; the body record's (unknown_key,
// missing_key, syntax, then body_mismatch) as BodyError; last the
// content's (basis to too_large, the 65,536-byte bound among them) as
// ContentError. A header with no body meets basis and scope alone, read
// from the header. try_check gives the same first fault as a value, with
// the body's digest left to the caller's bind, so its vectors run in
// constant evaluation too.
//
// Readings: the record is a separate argument, absent or present; given,
// it is read whatever the kind, so a forget's is body_mismatch. A record's
// parse fault comes before the header's own codes, a line's root that is
// no object among them. The content is parsed again from the record's
// bytes, its root at the value's first node as content's check reads it.
//
// Allocations: the line's value, canon's four, dropped; the record's
// value, four, dropped before the header is read; header's parse_line,
// four and one, the header's text, held; header's check, eight or ten,
// freed; the body's text, one, held; the content's value, four, and
// content's check, its walk's, freed. A header with no body: four, five,
// eight or ten, none. A size past json::max_input is MemoryError budget.
// Platform calls: the key's wipes alone (body's), each noexcept. check
// logs nothing.
//
// thread: none; an Entry, one (its body's key's platform's).

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>

#include "entry/body.hpp"
#include "entry/content.hpp"
#include "entry/header.hpp"
#include "memory/fail.hpp"
#include "platform/platform.hpp"
#include "qiven3/canon.hpp"
#include "qiven3/digest.hpp"

namespace qiven3::entry::check {

// The parts in part 1's order, each with its own codes.
enum class Part : std::uint8_t { parse, header, body, content };

// The first fault: its part and that part's code; a content fault whole,
// its path, limit and value with it.
struct Fault {
    Part part = Part::parse;
    std::uint8_t code = 0;
    content::ContentError error{};
};
#if INTPTR_MAX == INT64_MAX
static_assert(sizeof(Fault) == 128, "Fault's size is pinned: its part, its code and a ContentError");
#endif

// The fault's code as the format names it.
std::string_view name(const Fault& fault) noexcept;

// The fault thrown as its part's error: ParseError, HeaderError, BodyError
// or ContentError.
[[noreturn]] void raise(const Fault& fault);

// An entry part 1 admitted: its header, and its body unless it has none.
// Move-only.
template <platform::Platform P, typename A = std::allocator<char>>
class Entry {
public:
    using Body = body::Body<P, A>;

    Entry(header::Header<A> header, std::optional<Body> body) noexcept
        : header_(std::move(header)), body_(std::move(body)) {}
    Entry(const Entry&) = delete;
    Entry& operator=(const Entry&) = delete;
    Entry(Entry&&) noexcept = default;
    Entry& operator=(Entry&&) noexcept = default;
    ~Entry() = default;

    [[nodiscard]] const header::Header<A>& header() const noexcept { return header_; }
    [[nodiscard]] bool has_body() const noexcept { return body_.has_value(); }
    // The body; an entry with none is a fault of the caller's.
    [[nodiscard]] const Body& body() const {
        if (!body_) {
            memory::fail_fast("check: the body of an entry with none");
        }
        return *body_;
    }

private:
    header::Header<A> header_;
    std::optional<Body> body_;
};

namespace detail {

template <typename C>
constexpr Fault fault(Part part, C code) noexcept {
    return Fault{part, static_cast<std::uint8_t>(code), {}};
}

}  // namespace detail

// The first fault of a line and its record, or none; out holds the header
// once the line's root is an object. bind(parts, header) is called once
// the header and the record's layout hold, with the record's parts as
// views of record: it returns whether they bind to the header (the id the
// header's, and the content's digest under the key the header's content),
// keeping what it builds.
template <typename A, typename Bind>
constexpr std::optional<Fault> try_check(std::string_view line, std::optional<std::string_view> record,
                                         header::Header<A>& out, const Bind& bind, const A& allocator = A()) {
    bool object = false;
    {
        canon::Basic<A> value(allocator);
        if (const std::optional<ParseCode> code = canon::try_parse(line, value)) {
            return detail::fault(Part::parse, *code);
        }
        object = value.kind(0) == json::Kind::object;
    }
    std::optional<body::Code> layout;
    if (record) {
        canon::Basic<A> value(allocator);
        if (const std::optional<ParseCode> code = canon::try_parse(*record, value)) {
            return detail::fault(Part::parse, *code);
        }
        layout = body::detail::layout(value);
    }
    if (!object) {
        return detail::fault(Part::header, header::Code::syntax);
    }
    out = header::parse_line(line, allocator);
    if (const std::optional<header::Code> code = header::try_check(out)) {
        return detail::fault(Part::header, *code);
    }
    if (!record) {
        const canon::Basic<A> none(allocator);
        if (const std::optional<content::ContentError> e = content::try_check(out, none)) {
            return Fault{Part::content, static_cast<std::uint8_t>(e->code), *e};
        }
        return std::nullopt;
    }
    if (layout) {
        return detail::fault(Part::body, *layout);
    }
    const body::detail::RecordView parts = body::detail::parts(*record);
    if (!bind(parts, out)) {
        return detail::fault(Part::body, body::Code::body_mismatch);
    }
    canon::Basic<A> value(allocator);
    if (canon::try_parse(parts.content, value)) {
        memory::fail_fast("check: a canonical record's content not canonical");
    }
    if (const std::optional<content::ContentError> e = content::try_check(out, value)) {
        return Fault{Part::content, static_cast<std::uint8_t>(e->code), *e};
    }
    return std::nullopt;
}

// An entry from its stored header line and its body record, or none; the
// first fault thrown as its part's error. The body is built from the
// record's parts, its key read back by key_from_hex, and held against the
// header by body's check.
template <platform::Platform P, typename A = std::allocator<char>>
Entry<P, A> check_entry(P& platform, std::string_view line, std::optional<std::string_view> record,
                        const A& allocator = A()) {
    header::Header<A> h(allocator);
    std::optional<body::Body<P, A>> b;
    const auto bind = [&](const body::detail::RecordView& parts, const header::Header<A>& with) {
        b.emplace(parts.content, parts.id, digest::key_from_hex(platform, parts.key), allocator);
        try {
            body::check(*b, with);
        } catch (const body::BodyError&) {
            return false;
        }
        return true;
    };
    if (const std::optional<Fault> f = try_check(line, record, h, bind, allocator)) {
        raise(*f);
    }
    return Entry<P, A>(std::move(h), std::move(b));
}

}  // namespace qiven3::entry::check
