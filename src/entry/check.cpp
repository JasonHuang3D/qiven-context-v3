// check's names and throws, and its instantiation for v3's allocator. The
// part order's core, try_check, is a template on its bind too; check_entry
// on the platform, instantiated where a binary links its adapter.

#include "entry/check.hpp"

#include <string_view>

#include "memory/memory.hpp"

namespace qiven3::entry::check {

std::string_view name(const Fault& fault) noexcept {
    switch (fault.part) {
        case Part::parse: return qiven3::name(static_cast<ParseCode>(fault.code));
        case Part::header: return header::name(static_cast<header::Code>(fault.code));
        case Part::body: return body::name(static_cast<body::Code>(fault.code));
        case Part::content: return content::name(fault.error.code);
    }
    return "unknown";
}

void raise(const Fault& fault) {
    switch (fault.part) {
        case Part::parse: throw ParseError{static_cast<ParseCode>(fault.code)};
        case Part::header: throw header::HeaderError{static_cast<header::Code>(fault.code)};
        case Part::body: throw body::BodyError{static_cast<body::Code>(fault.code)};
        case Part::content: throw fault.error;
    }
    memory::fail_fast("check: a fault of no part");
}

}  // namespace qiven3::entry::check
