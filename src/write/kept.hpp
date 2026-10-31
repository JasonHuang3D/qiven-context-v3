#pragma once

// write/kept: the kept entry files, product. Every file a command appends
// or imports is kept in kept/ in the store's directory as kept/<n>.jsonl,
// n ten decimal digits one past the highest held, so the names sort in the
// order appended. A kept file holds one first line,
// {"kept":"qiven3-kept/1","n":<n>,"command":"import" or "write",
// "writer":<writer>,"entries":<count>}, then each entry's stored header
// line and its body record (none on a forget), in the order appended, each
// line ending in LF: what the store's import reads.
//
// A command keeps its file in four steps, under one lock, the platform's
// lock on kept/kept.lock, from settling to the last rename:
// 1. settle: each <n>.tmp, and the platform's own temporary of a replace
//    killed while writing one (.<n>.tmp.<16 hex>.tmp), removed unread;
//    each <n>.pending read: the store holding every id it names, renamed
//    <n>.jsonl; holding none, removed; holding a part, Refusal corrupt,
//    never settled;
// 2. keep: the file written whole and flushed as <n>.tmp by the platform's
//    replace, then renamed <n>.pending, so a pending file is never torn;
// 3. the caller's append (or the store's import);
// 4. done: on success renamed <n>.jsonl; on a Refusal the append throws
//    (one the store gives before it writes) removed; on any other fault,
//    the rename's included, left pending for the next command's step 1.
// A rename never replaces: a file already under the new name is the
// platform's exists, both kept. In step 2 that fails the command before
// its append, the <n>.tmp left for the next step 1; in step 4 the file is
// left pending. A held pending file whose <n>.jsonl is taken is Refusal
// corrupt in step 1, both left, never settled: moved, its first line would
// not name its n [reading: the smaller of a rewrite and a refusal; only a
// file made outside the lock takes that name]. The import keeps 0000000001
// by steps 2 to 4 alone, with no lock and no settle step: its directory
// held nothing, and after a fault it is discarded.
//
// Allocations: all the arena's: the paths, the listing, the file's bytes,
// a pending file read whole and its lines, and one work span of work_bytes
// over which each header line is parsed in an arena made afresh. Platform
// calls: create and lock (lock), list (settle, next), read, remove and
// rename (settle), replace and rename (keep), rename or remove (done). A
// lock held past its wait is Refusal busy; the platform's other errors and
// the holds callable's pass through. It logs nothing.
//
// thread: one (the platform's)

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "platform/platform.hpp"
#include "write/build.hpp"
#include "write/refuse.hpp"

namespace qiven3::write::kept {

using refuse::Code;
using refuse::Refusal;

inline constexpr std::string_view directory = "kept";
inline constexpr std::string_view lock_name = "kept.lock";
inline constexpr std::string_view kept_form = "qiven3-kept/1";
// The highest n ten digits hold; one more is Refusal store_full [reading].
inline constexpr std::uint64_t last = 9'999'999'999;
// The platform's default wait, for the lock and the renames.
inline constexpr platform::Seconds deadline{5.0};
// A stored header line's bound here: none reaches it, so one past it is
// corrupt; each is parsed within the work span.
inline constexpr std::size_t header_bytes = 4096;
inline constexpr std::size_t work_bytes = std::size_t{64} * 1024;

// The fault runs' hook: under QIVEN3_FAULTS a countdown each header line's
// work arena asks first.
struct Hooks {
#if defined(QIVEN3_FAULTS)
    memory::Countdown* work = nullptr;
#endif
};

enum class Command : std::uint8_t { import, write };
// "import" or "write".
std::string_view name(Command command) noexcept;

// A name in kept/: <n>.jsonl kept, <n>.pending, <n>.tmp, or replace's own
// temporary of a <n>.tmp.
enum class Form : std::uint8_t { kept, pending, tmp, temporary };
struct Name {
    std::uint64_t n = 0;
    Form form = Form::kept;
};
// The name's form and n, or none for any other name (the lock's among
// them).
std::optional<Name> parse_name(std::string_view name) noexcept;
// "<n, ten digits>.jsonl", ".pending" or ".tmp"; n past last or the form
// temporary is a caller's mistake: fail_fast.
memory::Inline<32> file_name(std::uint64_t n, Form form) noexcept;

// A kept file's first line.
struct Head {
    std::uint64_t n = 0;
    Command command = Command::write;
    memory::Inline<64> writer{};
    std::uint64_t entries = 0;
};

// One entry of a kept file read: views into the file's bytes, and its id.
struct LineView {
    std::string_view header;
    std::string_view body;
    bool has_body = false;
    memory::Inline<72> id{};
};

// The kept file's bytes into out, cleared first: head's line (its entries
// built.size()), then each entry's header line and its body record. A
// writer outside the writer's syntax's bytes (a-z, 0-9, '-', '/') or empty
// is a caller's mistake: fail_fast.
void bytes(const Head& head, std::span<const build::Built> built, memory::String& out);

// A kept or pending file's bytes read into head and lines, cleared first,
// views into bytes: its first line's form and its n the name's, then each
// entry's lines, a header line parsed for its id and whether a body follows,
// as many as the first line counts, at least one, every line ending in LF,
// nothing after. Any fault is Refusal corrupt naming kept/<name>; a
// refused allocation MemoryError, lines left empty.
void parse(std::string_view bytes, std::string_view name, memory::Arena& arena, Head& head,
           memory::Vector<LineView>& lines, Hooks hooks = {});

namespace detail {

// out, cleared first: the store's directory, then /kept, then /name when
// name is not empty.
void join(std::string_view store, std::string_view name, memory::String& out);
// The highest n among the names of any form, 0 for none.
std::uint64_t highest(std::span<const memory::String> names) noexcept;
// "kept/<name>", as a Refusal names it.
memory::Inline<128> at(std::string_view name) noexcept;

// kept/ made, one there no fault (create's exists; a file under the name
// fails list next).
template <platform::Platform P>
void make(P& platform, std::string_view store, memory::Arena& arena) {
    memory::String dir{memory::Allocator<char>(arena)};
    join(store, {}, dir);
    try {
        platform.create(dir, true);
    } catch (const platform::PlatformError& error) {
        if (error.code != platform::Code::exists) {
            throw;
        }
    }
}

template <platform::Platform P>
std::uint64_t listed(P& platform, std::string_view store, memory::Arena& arena, memory::Vector<memory::String>& names) {
    memory::String dir{memory::Allocator<char>(arena)};
    join(store, {}, dir);
    platform.list(dir, names);
    return highest(std::span<const memory::String>(names.data(), names.size()));
}

// Whether the store holds every id of the pending file at path: true all,
// false none, a part Refusal corrupt.
template <platform::Platform P, class H>
bool held(P& platform, std::string_view path, std::string_view name, H& holds, memory::Arena& arena,
          Hooks hooks) {
    memory::String data{memory::Allocator<char>(arena)};
    platform.read(path, data);
    Head head;
    memory::Vector<LineView> lines{memory::Allocator<LineView>(arena)};
    parse(data, name, arena, head, lines, hooks);
    std::size_t count = 0;
    for (const LineView& line : lines) {
        count += holds(line.id.view()) ? 1U : 0U;
    }
    if (count != 0 && count != lines.size()) {
        throw Refusal{Code::corrupt, at(name)};
    }
    return count != 0;
}

// Whether the file at path was removed: the platform's error false.
template <platform::Platform P>
bool removed(P& platform, std::string_view path) {
    try {
        platform.remove(path);
    } catch (const platform::PlatformError&) {
        return false;
    }
    return true;
}

}  // namespace detail

// What says whether the store holds an entry, by its id.
template <class H>
concept Holds = std::predicate<H&, std::string_view>;

// The kept files' lock: kept/ made if absent, then kept/kept.lock held,
// released by the handle's end. Held past wait: Refusal busy.
template <platform::Platform P>
auto lock(P& platform, std::string_view store, memory::Arena& arena, platform::Seconds wait = deadline) {
    detail::make(platform, store, arena);
    memory::String path{memory::Allocator<char>(arena)};
    detail::join(store, lock_name, path);
    try {
        return platform.lock(path, wait);
    } catch (const platform::PlatformError& error) {
        if (error.code == platform::Code::busy) {
            throw Refusal{Code::busy, detail::at(lock_name)};
        }
        throw;
    }
}

// Step 1, under the lock: every temporary removed unread, every pending
// file renamed kept or removed by its ids, as above; its kept name taken,
// Refusal corrupt naming it.
template <platform::Platform P, Holds H>
void settle(P& platform, std::string_view store, H& holds, memory::Arena& arena, Hooks hooks = {}) {
    memory::Vector<memory::String> names{memory::Allocator<memory::String>(arena)};
    static_cast<void>(detail::listed(platform, store, arena, names));
    memory::String path{memory::Allocator<char>(arena)};
    for (const memory::String& name : names) {
        const std::optional<Name> got = parse_name(name);
        if (!got || got->form == Form::kept) {
            continue;
        }
        detail::join(store, name, path);
        if (got->form != Form::pending || !detail::held(platform, path, name, holds, arena, hooks)) {
            platform.remove(path);
            continue;
        }
        try {
            platform.rename(path, file_name(got->n, Form::kept).view(), deadline);
        } catch (const platform::PlatformError& error) {
            if (error.code != platform::Code::exists) {
                throw;
            }
            throw Refusal{Code::corrupt, detail::at(name)};  // never moved: its first line names its n
        }
    }
}

// The next file's n: one past the highest name held. Past last: Refusal
// store_full.
template <platform::Platform P>
std::uint64_t next(P& platform, std::string_view store, memory::Arena& arena) {
    memory::Vector<memory::String> names{memory::Allocator<memory::String>(arena)};
    const std::uint64_t top = detail::listed(platform, store, arena, names);
    if (top >= last) {
        throw Refusal{Code::store_full, detail::at({})};
    }
    return top + 1;
}

// Step 2: data written whole and flushed as <n>.tmp, then renamed
// <n>.pending. A fault leaves no pending file; the <n>.tmp, or replace's
// temporary, is left for the next step 1.
template <platform::Platform P>
void keep(P& platform, std::string_view store, std::uint64_t n, std::string_view data, memory::Arena& arena) {
    memory::String path{memory::Allocator<char>(arena)};
    detail::join(store, file_name(n, Form::tmp).view(), path);
    platform.replace(path, data, deadline);
    platform.rename(path, file_name(n, Form::pending).view(), deadline);
}

// Steps 3 and 4: append() called; then <n>.pending renamed <n>.jsonl. A
// Refusal from append removes the pending file and goes on (a fault of
// that removal dropped: the next step 1 removes it, as it holds none of
// the store's ids); any other error leaves it pending and goes on.
template <platform::Platform P, class A>
void done(P& platform, std::string_view store, std::uint64_t n, A&& append, memory::Arena& arena) {
    memory::String path{memory::Allocator<char>(arena)};
    detail::join(store, file_name(n, Form::pending).view(), path);
    try {
        std::forward<A>(append)();
    } catch (const Refusal&) {
        static_cast<void>(detail::removed(platform, path));  // when not, the next step 1's
        throw;
    }
    platform.rename(path, file_name(n, Form::kept).view(), deadline);
}

// The write command's and a later file's (--after) kept file, steps 1 to
// 4 under the lock, the file's entries built; gives its n.
template <platform::Platform P, Holds H, class A>
std::uint64_t write(P& platform, std::string_view store, Command command, std::string_view writer,
                    std::span<const build::Built> built, H& holds, A&& append, memory::Arena& arena,
                    Hooks hooks = {}) {
    const auto guard = lock(platform, store, arena);
    settle(platform, store, holds, arena, hooks);
    const std::uint64_t n = next(platform, store, arena);
    {
        memory::String data{memory::Allocator<char>(arena)};
        bytes(Head{n, command, memory::Inline<64>(writer), built.size()}, built, data);
        keep(platform, store, n, data, arena);
    }
    done(platform, store, n, std::forward<A>(append), arena);
    return n;
}

// The import's kept file 0000000001 of writer import, by steps 2 to 4
// alone: kept/ made if absent, no lock, no settle step.
template <platform::Platform P, class A>
void import(P& platform, std::string_view store, std::span<const build::Built> built, A&& append,
            memory::Arena& arena) {
    detail::make(platform, store, arena);
    {
        memory::String data{memory::Allocator<char>(arena)};
        bytes(Head{1, Command::import, memory::Inline<64>("import"), built.size()}, built, data);
        keep(platform, store, 1, data, arena);
    }
    done(platform, store, 1, std::forward<A>(append), arena);
}

// What the rebuild reads: the kept files' names, <n>.jsonl alone, in
// order, into names, cleared first; each read whole and given to parse.
template <platform::Platform P>
void kept_files(P& platform, std::string_view store, memory::Vector<memory::String>& names) {
    memory::String dir{names.get_allocator()};
    detail::join(store, {}, dir);
    platform.list(dir, names);
    std::erase_if(names, [](const memory::String& name) {
        const std::optional<Name> got = parse_name(name);
        return !got || got->form != Form::kept;
    });
}

}  // namespace qiven3::write::kept
