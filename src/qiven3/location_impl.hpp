#pragma once

// location's templates on the platform, defined (qiven3/location.hpp
// declares them and states their rules): instantiation files alone include
// this header, a binary's for its one adapter and a case binary's for the
// fakes.

#include <array>
#include <cstddef>
#include <exception>
#include <optional>
#include <span>
#include <string_view>

#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "platform/platform.hpp"
#include "qiven3/location.hpp"
#include "store/errors.hpp"
#include "store/sqlite_store.hpp"

namespace qiven3::location {
namespace detail {

// A line to standard_output(), whose failure a caller decides.
template <platform::Platform P>
void say(P& platform, std::string_view line) {
    platform.standard_output().write(line);
}

// A line printed where the exit status carries the outcome: false where
// printing failed.
template <platform::Platform P>
bool said(P& platform, std::string_view line) noexcept {
    bool ok = false;
    try {
        say(platform, line);
        ok = true;
    } catch (const platform::PlatformError&) {
        ok = false;
    } catch (const memory::MemoryError&) {
        ok = false;
    }
    return ok;
}

// An error's line, best effort: the exit status carries the error.
template <platform::Platform P, typename E>
void report(P& platform, const E& error) noexcept {
    std::array<char, line_bytes> buffer{};
    static_cast<void>(said(platform, error_line(error, buffer)));
}

// The command arena's peak to hooks.peak at the run's end.
class Measure {
public:
    Measure(const memory::Arena& arena, std::size_t* peak) noexcept : arena_(&arena), peak_(peak) {}
    Measure(const Measure&) = delete;
    Measure& operator=(const Measure&) = delete;
    Measure(Measure&&) = delete;
    Measure& operator=(Measure&&) = delete;
    ~Measure() {
        if (peak_ != nullptr) {
            *peak_ = arena_->peak();
        }
    }

private:
    const memory::Arena* arena_;
    std::size_t* peak_;
};

// The record at path: its bytes into bytes and its dir into dir;
// StoreError location not_chosen where none is, config where its rules
// refuse it.
template <platform::Platform P>
void read_record(P& platform, std::string_view path, std::string_view op, String& bytes, String& dir) {
    if (absent(path)) {
        throw store::StoreError(store::Code::location, op, path, "not_chosen");
    }
    platform.read(path, bytes);
    parse_record(bytes, op, path, dir);
}

// The drive check on dir: one line per condition; true where any warns.
template <platform::Platform P>
bool check_drive(P& platform, std::string_view dir, memory::Arena& arena) {
    platform::Facts facts(arena);
    platform.facts(dir, facts);
    String line{memory::Allocator<char>(arena)};
    bool warned = false;
    const std::array<const platform::Reading*, 3> readings = {&facts.protection, &facts.file_history,
                                                              &facts.bitlocker};
    for (std::size_t i = 0; i < readings.size(); ++i) {
        warned = fact_line(static_cast<Condition>(i), *readings.at(i), facts.volume, line) || warned;
        say(platform, line);
    }
    return warned;
}

// Whether a and b, both resolved, are one directory: equal after normcase.
template <platform::Platform P>
bool same(P& platform, std::string_view a, std::string_view b, memory::Arena& arena) {
    String x{memory::Allocator<char>(arena)};
    String y{memory::Allocator<char>(arena)};
    platform.normcase(a, x);
    platform.normcase(b, y);
    return x == y;
}

// The store at dir opened as it is, or, where its file is missing, created;
// then closed. The engine's heap made for it where none is installed.
template <platform::Platform P>
void open_or_create(P& platform, std::string_view dir, memory::Arena& arena) {
    using Store = store::SQLiteStore<P>;
    std::optional<store::Engine<P>> engine;
    if (!store::sqlite::engine_ready()) {
        engine.emplace(platform, 1);
    }
    String file{memory::Allocator<char>(arena)};
    join(dir, store_name, file);
    bool missing = false;
    try {
        Store opened = Store::open(file, false, false, platform);
        static_cast<void>(opened.close());
    } catch (const store::StoreError& e) {
        if (e.code() != store::Code::missing) {
            throw;
        }
        missing = true;
    }
    if (missing) {
        Store made = Store::open(file, true, false, platform);
        static_cast<void>(made.close());
    }
}

// The record printed, then `recorded` naming dir: after the record is
// written, a failure to print leaves it recorded.
template <platform::Platform P>
int print_recorded(P& platform, std::string_view record, std::string_view dir, memory::Arena& arena) noexcept {
    if (!said(platform, record)) {
        return recorded;
    }
    try {
        String line{memory::Allocator<char>(arena)};
        line.append(recorded_prefix()).append(dir).push_back('\n');
        static_cast<void>(said(platform, line));
    } catch (const memory::MemoryError&) {
        return recorded;  // the line not composed; the record stands
    }
    return recorded;
}

// One command, under the lock; its errors to main.
template <platform::Platform P>
int run(P& platform, const ArgumentsView& args, const Hooks& hooks) {
    auto pages = platform.pages(budget);
    memory::Arena arena(pages.bytes());
    const Measure measure(arena, hooks.peak);
#if defined(QIVEN3_FAULTS)
    arena.attach(hooks.arenas);
#endif
    const memory::Allocator<char> on(arena);
    const std::string_view op = name(args.command);
    String config{on};
    platform.config_dir(true, config);
    String lock_path{on};
    join(config, lock_name, lock_path);
    auto held = platform.lock(lock_path, deadline);
    String path{on};
    join(config, record_name, path);
    String bytes{on};
    String old{on};
    if (args.command == Command::init) {
        if (!absent(path)) {
            throw store::StoreError(store::Code::location, op, path, "chosen");
        }
    } else {
        read_record(platform, path, op, bytes, old);
    }
    if (args.command == Command::show) {
        static_cast<void>(check_drive(platform, old, arena));
        return print_recorded(platform, bytes, old, arena);
    }
    String chosen{on};
    platform.resolve(args.directory, chosen);
    store::SQLiteStore<P>::guard(chosen, platform);
    if (check_drive(platform, chosen, arena) && !args.accept) {
        say(platform, not_accepted_line());
        return not_accepted;
    }
    if (args.command == Command::change) {
        if (same(platform, chosen, old, arena)) {
            return print_recorded(platform, bytes, old, arena);
        }
        left_behind(old, op);
    }
    record_bytes(platform.now(), chosen, op, bytes);
    open_or_create(platform, chosen, arena);
    if (args.command == Command::change) {
        say(platform, moved_line());
    }
    platform.replace(path, bytes, deadline);
    return print_recorded(platform, bytes, chosen, arena);
}

}  // namespace detail

template <platform::Platform P>
int main(std::span<const std::string_view> argv, P& platform, const Hooks& hooks) noexcept {
    const std::optional<ArgumentsView> args = arguments(argv);
    if (!args) {
        static_cast<void>(detail::said(platform, detail::usage_line()));
        return usage;
    }
    try {
        return detail::run(platform, *args, hooks);
    } catch (const store::StoreError& e) {
        detail::report(platform, e);
    } catch (const platform::PlatformError& e) {
        detail::report(platform, e);
    } catch (const memory::MemoryError& e) {
        detail::report(platform, e);
    } catch (const std::exception& e) {
        detail::report(platform, e);
    }
    return refused;
}

template <platform::Platform P>
void store_dir(P& platform, String& out) {
    const memory::Allocator<char> on = out.get_allocator();
    String config{on};
    platform.config_dir(false, config);
    String path{on};
    detail::join(config, record_name, path);
    String bytes{on};
    detail::read_record(platform, path, "store_dir", bytes, out);
}

}  // namespace qiven3::location
