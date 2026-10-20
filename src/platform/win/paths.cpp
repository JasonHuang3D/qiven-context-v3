#include "platform/win/paths.hpp"

#include <array>

namespace qiven3::platform::win::paths {
namespace {

constexpr wchar_t sep = L'\\';

bool is_sep(wchar_t c) noexcept { return c == L'\\' || c == L'/'; }

bool upper_equal(std::span<const wchar_t> text, std::wstring_view upper) noexcept {
    if (text.size() < upper.size()) {
        return false;
    }
    for (std::size_t i = 0; i < upper.size(); ++i) {
        const wchar_t c = text[i];
        const wchar_t folded = (c >= L'a' && c <= L'z') ? static_cast<wchar_t>(c - 32) : c;
        if (folded != upper[i]) {
            return false;
        }
    }
    return true;
}

// The drive's and the root's lengths, as ntpath's splitroot gives them, of
// a path holding '\' alone as its separator.
struct Root {
    std::size_t drive = 0;
    std::size_t root = 0;
};

std::size_t find(std::span<const wchar_t> text, std::size_t from) noexcept {
    for (std::size_t i = from; i < text.size(); ++i) {
        if (text[i] == sep) {
            return i;
        }
    }
    return text.size();
}

Root split_root(std::span<const wchar_t> n) noexcept {
    if (!n.empty() && n[0] == sep) {
        if (n.size() >= 2 && n[1] == sep) {
            const std::size_t from = upper_equal(n, L"\\\\?\\UNC\\") ? 8 : 2;
            const std::size_t index = find(n, from);
            if (index == n.size()) {
                return {n.size(), 0};
            }
            const std::size_t index2 = find(n, index + 1);
            if (index2 == n.size()) {
                return {n.size(), 0};
            }
            return {index2, 1};
        }
        return {0, 1};
    }
    if (n.size() >= 2 && n[1] == L':') {
        return {2, n.size() >= 3 && n[2] == sep ? std::size_t{1} : std::size_t{0}};
    }
    return {0, 0};
}

// Where the last component written to path's first `end` units starts, no
// earlier than `base`.
std::size_t part_start(std::span<const wchar_t> path, std::size_t base, std::size_t end) noexcept {
    while (end > base && path[end - 1] != sep) {
        --end;
    }
    return end;
}

bool dots(std::span<const wchar_t> path, std::size_t at, std::size_t length) noexcept {
    return length == 2 && path[at] == L'.' && path[at + 1] == L'.';
}

}  // namespace

std::span<wchar_t> scratch(const WindowsPages& pages) noexcept {
    return std::span<wchar_t>(static_cast<wchar_t*>(pages.address()), pages.bytes().size() / sizeof(wchar_t));
}

bool control(wchar_t c) noexcept { return c < 0x20 || (c >= 0x7F && c <= 0x9F); }

bool starts(std::span<const wchar_t> text, std::wstring_view prefix) noexcept {
    return std::wstring_view(text.data(), text.size()).starts_with(prefix);
}

bool absolute(std::span<const wchar_t> path) noexcept {
    // The first three characters; a surrogate pair is one, and none of the
    // rule's.
    std::array<wchar_t, 3> c{};
    std::size_t at = 0;
    for (wchar_t& one : c) {
        if (at >= path.size()) {
            break;
        }
        const wchar_t unit = path[at];
        const bool pair = unit >= 0xD800 && unit <= 0xDBFF && at + 1 < path.size() && path[at + 1] >= 0xDC00 &&
                          path[at + 1] <= 0xDFFF;
        one = pair ? wchar_t{0xFFFF} : (unit == L'/' ? sep : unit);
        at += pair ? 2 : 1;
    }
    return (c[0] == sep && c[1] == sep) || (c[1] == L':' && c[2] == sep);
}

std::size_t normal(std::span<wchar_t> path, std::size_t m) noexcept {
    for (std::size_t i = 0; i < m; ++i) {
        if (path[i] == L'/') {
            path[i] = sep;
        }
    }
    const Root r = split_root(path.first(m));
    const std::size_t base = r.drive + r.root;
    std::size_t w = base;  // the write cursor, never past the read
    std::size_t parts = 0;
    bool last_dots = false;
    for (std::size_t at = base; at <= m;) {
        std::size_t end = at;
        while (end < m && path[end] != sep) {
            ++end;
        }
        const std::size_t from = at;
        const std::size_t length = end - at;
        at = end + 1;
        if (length == 0 || (length == 1 && path[from] == L'.')) {
            continue;
        }
        const bool up = dots(path, from, length);
        if (up && parts > 0 && !last_dots) {
            const std::size_t start = part_start(path, base, w);
            w = start > base ? start - 1 : base;
            --parts;
            const std::size_t before = part_start(path, base, w);
            last_dots = parts > 0 && dots(path, before, w - before);
            continue;
        }
        if (!up || parts > 0 || r.root == 0) {
            if (parts > 0) {
                path[w++] = sep;
            }
            for (std::size_t k = 0; k < length; ++k) {
                path[w++] = path[from + k];
            }
            ++parts;
            last_dots = up;
        }
    }
    if (w == 0) {
        path[w++] = L'.';
    }
    path[w] = L'\0';
    return w;
}

Split split(std::span<const wchar_t> path) noexcept {
    const Root r = split_root(path);
    const std::size_t base = r.drive + r.root;
    const std::size_t name = part_start(path, base, path.size());
    std::size_t head = name;
    while (head > base && is_sep(path[head - 1])) {
        --head;
    }
    return {head, name};
}

std::size_t append(std::span<wchar_t> into, std::size_t at, std::span<const wchar_t> text) {
    if (at + text.size() >= into.size()) {
        throw PlatformError(Code::path, "resolve", "longer than 32,767 UTF-16 units once resolved");
    }
    for (const wchar_t unit : text) {
        into[at++] = unit;
    }
    return at;
}

std::size_t join(std::span<wchar_t> into, std::size_t f, std::span<const wchar_t> rest) {
    if (rest.empty()) {
        return f;
    }
    if (f > 0 && !is_sep(into[f - 1]) && !(f == 2 && into[1] == L':')) {
        f = append(into, f, std::span<const wchar_t>(&sep, 1));
    }
    return append(into, f, rest);
}

bool on_letter(std::span<const wchar_t> path) noexcept {
    return path.size() >= 3 &&
           ((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z')) && path[1] == L':' &&
           path[2] == sep;
}

}  // namespace qiven3::platform::win::paths
