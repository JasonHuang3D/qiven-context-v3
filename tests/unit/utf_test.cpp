// utf's cases: the strict decoder's vectors, code points and UTF-16 units,
// code point order, a system name's lone surrogates, the text's fixed
// ranges, and decode's budget and its allocations failed in turn.

#include <array>
#include <compare>
#include <cstddef>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "qiven3/utf.hpp"
#include "runner/runner.hpp"
#include "support/faults/faults.hpp"

namespace {

using qiven3::ParseCode;
using qiven3::ParseError;
using qiven3::memory::Allocator;
using qiven3::memory::Arena;
using qiven3::memory::MemoryError;
namespace utf = qiven3::utf;

// The UTF-16 units of a valid text, the order RFC 8785 sorts by.
std::u16string utf16(std::string_view text) {
    std::vector<char32_t> points;
    utf::decode(text, points);
    std::u16string out;
    for (const char32_t p : points) {
        if (p < 0x10000U) {
            out.push_back(static_cast<char16_t>(p));
        } else {
            out.push_back(static_cast<char16_t>(0xD800U + ((p - 0x10000U) >> 10U)));
            out.push_back(static_cast<char16_t>(0xDC00U + ((p - 0x10000U) & 0x3FFU)));
        }
    }
    return out;
}

int sign(std::strong_ordering o) { return o < 0 ? -1 : o > 0 ? 1 : 0; }

int sign16(std::string_view a, std::string_view b) {
    const std::u16string x = utf16(a);
    const std::u16string y = utf16(b);
    return x < y ? -1 : y < x ? 1 : 0;
}

std::optional<ParseCode> thrown(std::string_view data, utf::Form form, std::vector<char32_t>& out) {
    try {
        utf::decode(data, out, form);
    } catch (const ParseError& e) {
        return e.code;
    }
    return std::nullopt;
}

struct Valid {
    std::string_view text;
    std::size_t points;
    std::size_t units;
};

}  // namespace

QIVEN3_CASE(utf, valid_vectors) {
    const std::array<Valid, 9> valid{{
        {"", 0, 0},
        {"\x61", 1, 1},
        {"\x63\x61\x66\xc3\xa9", 4, 4},
        {"\xf0\x9d\x84\x9e", 1, 2},
        {"\xf4\x8f\xbf\xbf", 1, 2},
        {"\xef\xbb\xbf", 1, 1},
        {"\xe2\x80\xa8", 1, 1},
        {"\xef\xbf\xbe", 1, 1},
        {"\x61\xc3\xa9\xe4\xb8\xad\xf0\x9f\x98\x80", 4, 5},
    }};
    for (const Valid& v : valid) {
        CHECK(!utf::check(v.text));
        CHECK_EQ(utf::length(v.text), v.points);
        CHECK_EQ(utf::units16(v.text), v.units);
        std::vector<char32_t> points;
        CHECK(!thrown(v.text, utf::Form::strict, points));
        CHECK_EQ(points.size(), v.points);
        CHECK_EQ(utf16(v.text).size(), v.units);
    }
    std::vector<char32_t> points;
    utf::decode("\x61\xc3\xa9\xe4\xb8\xad\xf0\x9f\x98\x80", points);
    CHECK(points == (std::vector<char32_t>{0x61, 0xE9, 0x4E2D, 0x1F600}));
}

QIVEN3_CASE(utf, refused_vectors) {
    const std::array<std::string_view, 12> refused{{
        "\xff", "\xc0\x80", "\xc1\xbf", "\xe0\x80\x80", "\xed\xa0\x80", "\xed\xbf\xbf",
        "\xf4\x90\x80\x80", "\xf5\x80\x80\x80", "\x80", "\xe2\x82", "\xf0\x9d\x84", "\xf0\x8f\xbf\xbf",
    }};
    for (const std::string_view r : refused) {
        CHECK(utf::check(r) == ParseCode::utf8);
        // Out keeps what it held.
        std::vector<char32_t> out{0x41};
        CHECK(thrown(r, utf::Form::strict, out) == ParseCode::utf8);
        CHECK(out == std::vector<char32_t>{0x41});
        // Inside a text, from its first fault.
        const std::string inside = std::string("a") + std::string(r) + "b";
        CHECK(utf::check(inside) == ParseCode::utf8);
    }
}

QIVEN3_CASE(utf, value_form) {
    // The parser keeps a lone \u surrogate in its three-byte form, which the
    // value form reads as surrogate and the strict form as utf8.
    std::vector<char32_t> out;
    CHECK(utf::check("a\xed\xa0\x80", utf::Form::value) == ParseCode::surrogate);
    CHECK(thrown("\xed\xbf\xbf", utf::Form::value, out) == ParseCode::surrogate);
    CHECK(thrown("\xed\xbf\xbf", utf::Form::strict, out) == ParseCode::utf8);
    // A utf8 fault before the surrogate comes first.
    CHECK(utf::check("\xff\xed\xa0\x80", utf::Form::value) == ParseCode::utf8);
    CHECK(utf::check("\xed\x9f\xbf", utf::Form::value) == std::nullopt);
    std::string s;
    utf::append(s, 0xD834);
    CHECK(s == "\xed\xa0\xb4");
    CHECK(out.empty());
}

QIVEN3_CASE(utf, order_vectors) {
    struct Pair {
        std::string_view a;
        std::string_view b;
        int by_point;
        int by_unit;
    };
    const std::array<Pair, 6> pairs{{
        {"a_b", "aa", -1, -1},
        {"", "a", -1, -1},
        {"\xc3\xa9", "z", 1, 1},
        {"project:b", "project:a-z", 1, 1},
        {"\xef\xbf\xbd", "\xf0\x9f\x98\x80", -1, 1},
        {"\xee\x80\x80", "\xf0\x90\x80\x80", -1, 1},
    }};
    for (const Pair& p : pairs) {
        CHECK_EQ(sign(utf::compare(p.a, p.b)), p.by_point);
        CHECK_EQ(sign(utf::compare(p.b, p.a)), -p.by_point);
        CHECK_EQ(sign16(p.a, p.b), p.by_unit);
        // std::string's own order is the same: char compared as unsigned.
        CHECK_EQ(sign(std::string(p.a).compare(std::string(p.b)) <=> 0), p.by_point);
    }
    CHECK(utf::compare("ab", "ab") == std::strong_ordering::equal);
}

QIVEN3_CASE(utf, backslashreplace_vectors) {
    struct Name {
        std::u16string_view name;
        std::string_view out;
    };
    const std::array<Name, 6> names{{
        {u"a\xdc80" u"b", "a\\udc80b"},
        {u"\xd800", "\\ud800"},
        {u"\xdc80\xd800", "\\udc80\\ud800"},
        {u"\xd83d\xde00", "\xf0\x9f\x98\x80"},
        {u"\x00e9", "\xc3\xa9"},
        {u"", ""},
    }};
    for (const Name& n : names) {
        std::string out;
        utf::backslashreplace(n.name, out);
        CHECK(out == n.out);
    }
    // UTF-16LE 610080dc6200 gives the bytes 615c756463383062.
    std::string out;
    utf::backslashreplace(u"a\xdc80" u"b", out);
    CHECK(out == "\x61\x5c\x75\x64\x63\x38\x30\x62");
}

QIVEN3_CASE(utf, ranges) {
    std::size_t surrogates = 0;
    std::size_t nonchars = 0;
    for (char32_t p = 0; p <= 0x10FFFFU; ++p) {
        surrogates += utf::is_surrogate(p) ? 1U : 0U;
        nonchars += utf::is_nonchar(p) ? 1U : 0U;
    }
    CHECK_EQ(surrogates, 2048U);
    CHECK_EQ(nonchars, 66U);
    CHECK(utf::is_nonchar(0xFDD0) && utf::is_nonchar(0xFDEF) && !utf::is_nonchar(0xFDF0));
    CHECK(utf::is_nonchar(0x1FFFE) && utf::is_nonchar(0x10FFFF) && !utf::is_nonchar(0xFFFD));
    // Every code point's UTF-8 reads back as itself, a surrogate in the
    // value form alone.
    std::size_t wrong = 0;
    for (char32_t p = 0; p <= 0x10FFFFU; ++p) {
        std::string s;
        utf::append(s, p);
        const utf::Read r = utf::next(s, 0, utf::Form::value);
        const bool strict_ok = utf::next(s, 0, utf::Form::strict).size == s.size();
        wrong += r.point == p && r.size == s.size() && strict_ok != utf::is_surrogate(p) ? 0U : 1U;
    }
    CHECK_EQ(wrong, 0U);
}

QIVEN3_CASE(utf, decode_budget) {
    // the specification's budget for decode: 64 KiB plus 4 times the input's
    // bytes, into the caller's vector on the arena; measured here.
    std::string text;
    for (int i = 0; i < 2000; ++i) {
        text += "a\xc3\xa9\xe4\xb8\xad\xf0\x9f\x98\x80";
    }
    const std::size_t budget = 65536 + 4 * text.size();
    Arena& arena = qiven3_ctx.counted.arena(budget);
    std::size_t peak = 0;
    {
        qiven3::memory::Vector<char32_t> out{Allocator<char32_t>(arena)};
        utf::decode(text, out);
        CHECK_EQ(out.size(), 8000U);
        peak = arena.peak();
    }
    std::cout << "utf.decode_budget: input " << text.size() << " bytes, peak " << peak << ", budget " << budget
              << "\n";
    CHECK(peak <= budget);
    CHECK(peak >= 4 * 8000U);
    // A byte short: MemoryError budget, the vector unchanged.
    Arena& shorter = qiven3_ctx.counted.arena(peak - 1);
    qiven3::memory::Vector<char32_t> out{Allocator<char32_t>(shorter)};
    bool refused = false;
    try {
        utf::decode(text, out);
    } catch (const MemoryError& e) {
        refused = e.code == qiven3::memory::MemoryCode::budget;
    }
    CHECK(refused);
    CHECK(out.empty());
}

QIVEN3_CASE(utf, decode_faults) {
#if defined(QIVEN3_FAULTS) && (!defined(_ITERATOR_DEBUG_LEVEL) || _ITERATOR_DEBUG_LEVEL == 0)
    std::vector<std::byte> pages(65536);
    qiven3::memory::Countdown countdown;
    std::optional<Arena> arena;
    std::size_t decoded = 0;
    const auto check = [&](std::size_t) { return arena->used() == 0; };
    const auto op = [&] {
        arena.emplace(pages);
        arena->attach(&countdown);
        qiven3::memory::Vector<char32_t> out{Allocator<char32_t>(*arena)};
        utf::decode("caf\xc3\xa9 \xf0\x9d\x84\x9e", out);
        decoded = out.size();
    };
    const qiven3::test::FaultRuns runs = qiven3::test::run_faults<MemoryError>(countdown, op, check);
    std::cout << "utf.decode_faults: allocations " << runs.typed << ", completed at " << runs.completed_at << "\n";
    CHECK_EQ(runs.typed, 1U);
    CHECK_EQ(runs.completed_at, 2U);
    CHECK_EQ(runs.untyped, 0U);
    CHECK_EQ(runs.unchecked, 0U);
    CHECK_EQ(decoded, 6U);
    arena.reset();
#else
    QIVEN3_SKIP("not applicable: built without QIVEN3_FAULTS, or with MSVC's debug containers");
#endif
}
