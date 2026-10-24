#pragma once

// The Windows adapter's path forms in UTF-16, on the spans of its scratch:
// v3's absolute rule, ntpath's normpath and split, the join of a final path
// and the rest. Each works in place or into the caller's span and takes no
// memory of its own; none calls Windows. The adapter's sources alone
// include this header.
//
// thread: none

#include <cstddef>
#include <span>
#include <string_view>

#include "platform/win/windows.hpp"

namespace qiven3::platform::win::paths {

// A path's buffer: 32,767 units and a null.
inline constexpr std::size_t units = 32768;

// The adapter's scratch as UTF-16 units: two buffers of `units`, then the
// configuration override's units.
std::span<wchar_t> scratch(const WindowsPages& pages) noexcept;

// U+0000 to U+001F and U+007F to U+009F.
bool control(wchar_t c) noexcept;

bool starts(std::span<const wchar_t> text, std::wstring_view prefix) noexcept;

// v3's absolute rule: the first three characters, '/' as '\', start "\\"
// or hold ":\" at the second.
bool absolute(std::span<const wchar_t> path) noexcept;

// ntpath's normpath, in place on the first m units: '/' as '\', empty and
// '.' components dropped, '..' folded, a '..' at the root dropped; "." for
// nothing. Its length; a null follows it.
std::size_t normal(std::span<wchar_t> path, std::size_t m) noexcept;

// ntpath's split of a path made normal: `name`, where its last component
// starts (the path's length when it has none), and `head`, the length of
// what precedes it, its trailing separators stripped but the root's.
struct Split {
    std::size_t head = 0;
    std::size_t name = 0;
};
Split split(std::span<const wchar_t> path) noexcept;

// `text` appended to `into` at `at`: the new length; past the buffer,
// PlatformError path.
std::size_t append(std::span<wchar_t> into, std::size_t at, std::span<const wchar_t> text);

// The rest joined to a final path of length f, with a separator unless the
// final path ends in one or is a bare drive: the new length.
std::size_t join(std::span<wchar_t> into, std::size_t f, std::span<const wchar_t> rest);

// "X:\" and beyond, X an ASCII letter.
bool on_letter(std::span<const wchar_t> path) noexcept;

}  // namespace qiven3::platform::win::paths
