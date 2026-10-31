#pragma once

// checked/part1: part 1 of the shared validator over one entry, as
// check_entry reads it, for the rules' read and a held line read again; a
// fault refused EntryInvalid in the store's words, with its code, its id
// once the header is read through (a body or content fault), the field's
// path for a content fault.
//
//   Header     a header on the call's allocator
//   Entry<P>   an entry part 1 read: its header and its body
//   part1(platform, at, index, line, allocator)   part 1 of one entry
//   refuse_part1(at, index, id, fault)            its refusal (rules.cpp)
//
// Allocations: the parse's and the body's on the allocator given.
// Platform calls: the body's key's and part 1's wipes, each noexcept.
// thread: none.

#include <cstddef>
#include <optional>
#include <string_view>
#include <utility>

#include "entry/body.hpp"
#include "entry/check.hpp"
#include "entry/header.hpp"
#include "memory/memory.hpp"
#include "platform/platform.hpp"
#include "qiven3/digest.hpp"
#include "store/contract.hpp"

namespace qiven3::checked {

using Header = entry::header::Header<memory::Allocator<char>>;

template <platform::Platform P>
using Entry = entry::check::Entry<P, memory::Allocator<char>>;

[[noreturn]] void refuse_part1(const store::AtView& at, std::size_t index, std::string_view id,
                               const entry::check::Fault& fault);

template <platform::Platform P>
Entry<P> part1(P& platform, const store::AtView& at, std::size_t index, const store::EntryView& line,
               const memory::Allocator<char>& allocator) {
    namespace body = entry::body;
    using entry::header::Slot;
    Header h(allocator);
    std::optional<body::Body<P, memory::Allocator<char>>> b;
    const auto bind = [&](const body::detail::RecordView& parts, const Header& with) {
        b.emplace(parts.content, parts.id, digest::key_from_hex(platform, parts.key), allocator);
        try {
            body::check(*b, with);
        } catch (const body::BodyError&) {
            return false;
        }
        return true;
    };
    if (const std::optional<entry::check::Fault> f = entry::check::try_check(line.line, line.body, h, bind, allocator)) {
        const bool read = f->part == entry::check::Part::body || f->part == entry::check::Part::content;
        refuse_part1(at, index, read ? h.value(Slot::id) : std::string_view(), *f);
    }
    return Entry<P>(std::move(h), std::move(b));
}

}  // namespace qiven3::checked
