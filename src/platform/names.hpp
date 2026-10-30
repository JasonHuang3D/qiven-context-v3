#pragma once

// files' list, remove and rename over a System's calls (platform/files.hpp):
// a directory's names sorted by their UTF-8 bytes, a file deleted with none
// there no fault, and a file given a new name in its directory, never
// replacing one there, retried as replace's rename is.
//
// list takes the caller's arena for its names alone; remove and rename
// take no memory.
//
// thread: one (a System serves one thread at a time)

#include <chrono>
#include <cstddef>
#include <string_view>

#include "platform/files.hpp"
#include "platform/platform.hpp"

namespace qiven3::platform::files {

// names[root]'s heap below end restored, a parent never before a child in
// std::string_view's order (unsigned bytes': UTF-8's code points).
inline void sift(Vector<String>& names, std::size_t root, std::size_t end) noexcept {
    for (std::size_t child = (2 * root) + 1; child < end; child = (2 * root) + 1) {
        if (child + 1 < end && std::string_view(names[child]) < std::string_view(names[child + 1])) {
            ++child;
        }
        if (!(std::string_view(names[root]) < std::string_view(names[child]))) {
            return;
        }
        names[root].swap(names[child]);
        root = child;
    }
}

// names sorted by their UTF-8 bytes, a heap sort by swaps alone: no String
// is moved from, as std::sort's insertion step does, and none allocates.
inline void sort_names(Vector<String>& names) noexcept {
    const std::size_t size = names.size();
    for (std::size_t root = size / 2; root-- > 0;) {
        sift(names, root, size);
    }
    for (std::size_t end = size; end-- > 1;) {
        names[0].swap(names[end]);
        sift(names, 0, end);
    }
}

// The names directly in directory into names, cleared first: never . or
// .., sorted by their UTF-8 bytes whatever order the system gives them in,
// so the same order on every host. Each is a String on names' arena, the
// arena's budget their bound; a refused allocation's MemoryError leaves
// names cleared, as each error does. A name the system cannot give as
// UTF-8 (1113) is path, naming the directory; any other error io.
template <System S>
void list(S& system, std::string_view directory, Vector<String>& names) {
    names.clear();
    check(system, "list", directory, true);
    Error error = none;
    try {
        error = system.list_names(directory, names);
    } catch (...) {
        names.clear();
        throw;
    }
    if (error != none) {
        names.clear();
        Detail detail;
        add_error(detail, "list", error);
        if (error == no_unicode_translation) {
            detail.add(", a name in ").add(directory);
        }
        throw PlatformError(error == no_unicode_translation ? Code::path : Code::io, "list", detail);
    }
    std::erase_if(names, [](const String& name) { return name == "." || name == ".."; });
    sort_names(names);
}

// The file at path deleted; none there (2, 3) no fault. Held without
// sharing delete (32): busy, the file kept, with no retry; any other
// error io.
template <System S>
void remove(S& system, std::string_view path) {
    check(system, "remove", path, true);
    const Error error = system.remove_file(path);
    if (error != none && !absent(error)) {
        raise(error == sharing_violation ? Code::busy : Code::io, "remove", "delete", error);
    }
}

// The file at path renamed to name in its directory, never replacing:
// one there (80, 183) is exists, both kept. Refused with 5 or 32, retried
// every 50 ms until the deadline, then busy, both kept; a deadline of 0 or
// not a number gets one try. Any other error io.
template <System S>
void rename(S& system, std::string_view path, std::string_view name, Seconds deadline) {
    check(system, "rename", path, true);
    refuse("rename", name_fault(name));
    const std::chrono::milliseconds start = system.elapsed();
    for (;;) {
        const Error error = system.rename_file(path, name);
        if (error == none) {
            return;
        }
        if (!retried(error) || spent(system.elapsed() - start, deadline)) {
            raise(present(error) ? Code::exists : retried(error) ? Code::busy : Code::io, "rename", "rename", error);
        }
        system.pause(interval);
    }
}

}  // namespace qiven3::platform::files
