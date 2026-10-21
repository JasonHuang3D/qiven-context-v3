#pragma once

// line: one event's JSON line, by json::encode alone, over the caller's
// string type, so that its vectors run in constant evaluation too. The
// line is one object: the head, event, level, log, op and t, and the
// event's fields, its keys in code point order as encode writes them. It
// takes four allocations from out's allocator: the value's nodes and text,
// each reserved whole, encode's sorted members and the bytes.
//
// thread: none

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "log/log.hpp"
#include "qiven3/json.hpp"

namespace qiven3::log {

// A line's head: borrowed for one event.
struct HeadView {
    std::string_view level;
    std::string_view event;
    std::uint64_t log = 0;
    std::uint64_t op = 0;
    std::uint64_t t = 0;
};

namespace detail {
constexpr std::int64_t number(std::uint64_t value) noexcept {
    // Count and Millis saturate at JSON's range; a number past it is the
    // caller's broken invariant, written saturated as well.
    return static_cast<std::int64_t>(value > max_number ? max_number : value);
}
}  // namespace detail

// The line of head and fields appended to out, a string type. Past out's
// arena's budget, MemoryError budget.
template <typename S>
constexpr void encode_line(const HeadView& head, std::span<const FieldView> fields, S& out) {
    using A = typename S::allocator_type;
    // The head's keys, event level log op t, are 16 bytes.
    std::size_t text = 16 + head.level.size() + head.event.size();
    for (const FieldView& f : fields) {
        text += f.key.size() + f.text.size();
    }
    json::Basic<A> value(out.get_allocator());
    value.reserve(1 + 2 * (5 + fields.size()), text);
    json::Writer<A> w(value);
    w.open(json::Kind::object);
    w.string("event");
    w.string(head.event);
    w.string("level");
    w.string(head.level);
    w.string("log");
    w.integer(detail::number(head.log));
    w.string("op");
    w.integer(detail::number(head.op));
    w.string("t");
    w.integer(detail::number(head.t));
    for (const FieldView& f : fields) {
        w.string(f.key);
        if (f.is_text) {
            w.string(f.text);
        } else {
            w.integer(detail::number(f.number));
        }
    }
    w.close();
    json::encode(value, out);
}

}  // namespace qiven3::log
