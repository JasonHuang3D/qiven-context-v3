#pragma once

// location: the human's choice of the store's directory. Core never picks
// one and no path is a default: `qiven3 location init DIRECTORY`, the first
// choice, `change DIRECTORY`, the human's change, and `show`, the record
// and its drive's facts as read now, each taking `--config DIR` and init
// and change `--accept`. main runs one command on a platform: it makes the
// configuration directory (config_dir(true)) and holds `lock` on
// location.lock there until it ends. init and change check the chosen
// path: it resolves (PlatformError path refuses it); the store's guard
// refuses it in or under a checkout or a synced folder (StoreError
// location, checkout or synced); the drive's facts print one line per
// condition, and a warning without --accept records nothing and exits 4.
// init refuses a record already there (chosen); change takes the first of
// its rules that holds: the recorded directory again, nothing changed; the
// recorded one still holding any of v3's files (not_moved, naming them) or
// unreadable (old_unreadable, naming the error); then, as at init, a
// store.db in the directory opened as it is, or a store created there,
// then the record written, last, by replace. A change moves, copies and
// deletes no file. Output goes through standard_output(): the check's
// lines, then the record, then `recorded`, naming the directory; a failure
// to print after the record leaves it recorded and exits 0. Exit status: 0
// recorded or shown, 1 refused, 2 a usage error, 4 warnings not accepted.
//
// The record, location.json in the configuration directory: one canonical
// JSON line and its LF, of exactly `at`, the time of the choice, and
// `dir`, the directory as resolve gives it, absolute with no control
// character; read by the strict parser on the bytes before the LF. A
// record refused is StoreError location, config; none is not_chosen.
// store_dir reads it for the commands that open the live store.
//
// argv is the command's own arguments, from the subcommand on ("init",
// DIRECTORY, ...). The configuration directory is the platform's, an
// override given where the platform is made: a binary's main reads
// `--config` by config_option and makes its platform with it, so main
// accepts the option and leaves its value to the platform.
//
// Budget: a command's own memory, 1 MiB (the store's operations' figure)
// on an arena over the platform's pages, counted by it; a store open or
// create its own operation within its own budget, and the engine's heap
// apart, one connection's, made here when none is installed. store_dir:
// the caller's arena, the record's bytes and its parsed value. The
// standard library's file system reads (the record's presence, each of the
// old directory's files by its status) allocate on its own heap, uncounted.
// Platform calls: pages, config_dir, lock, then by the command read,
// resolve, the guard's pages, resolve and synced, facts, normcase, now,
// the engine's pages, the store's open, replace, and standard_output for
// each line.
//
// thread: one; the lock on location.lock orders the commands of separate
// processes, each waiting up to 5 s. Exception guarantee: a refusal
// changes no record; a store created before a failed record stays, and
// the next init or change opens it. main throws nothing it can catch: a
// typed error is exit 1 and one line naming it.

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <optional>
#include <span>
#include <string_view>

#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "platform/platform.hpp"
#include "qiven3/utc.hpp"
#include "store/errors.hpp"
#include "store/sqlite_store.hpp"

namespace qiven3::location {

using memory::String;

// The text's names: the record, its lock, the store's file.
inline constexpr std::string_view record_name = "location.json";
inline constexpr std::string_view lock_name = "location.lock";
inline constexpr std::string_view store_name = "store.db";
// The lock's and the record's replace's deadline.
inline constexpr platform::Seconds deadline{5.0};
// A command's own memory, the store's operations' figure.
inline constexpr std::size_t budget = store::operation_base;

// The exit statuses; never 3, which an abort gives.
inline constexpr int recorded = 0;
inline constexpr int refused = 1;
inline constexpr int usage = 2;
inline constexpr int not_accepted = 4;

enum class Command : std::uint8_t { init, change, show };

// The command's name, as the text spells it, the StoreError's operation.
constexpr std::string_view name(Command command) noexcept {
    constexpr std::array<std::string_view, 3> names = {"init", "change", "show"};
    return names.at(static_cast<std::size_t>(command));
}

// A command line read: views of argv's text, used within main's call.
struct ArgumentsView {
    std::string_view directory;  // empty for show
    std::string_view config;     // empty when not given
    Command command = Command::show;
    bool accept = false;
};
#if INTPTR_MAX == INT64_MAX
static_assert(sizeof(ArgumentsView) == 40, "ArgumentsView's size is pinned: two views, the command and --accept");
#endif

// argv read as `init DIRECTORY [--accept] [--config DIR]`, `change` the
// same, or `show [--config DIR]`, the options in any order after the
// subcommand; none for a usage error: no subcommand or another, a
// DIRECTORY missing, given to show or twice, an option unknown or twice,
// `--accept` to show, `--config` with no value or an empty one.
[[nodiscard]] std::optional<ArgumentsView> arguments(std::span<const std::string_view> argv) noexcept;

// `--config`'s value, for the binary's main that makes the platform; none
// when argv is a usage error or names none.
[[nodiscard]] std::optional<std::string_view> config_option(std::span<const std::string_view> argv) noexcept;

// A run's hooks, the tests' alone: peak, where given, takes the command
// arena's peak at its end; under QIVEN3_FAULTS the arena asks arenas
// first.
struct Hooks {
    std::size_t* peak = nullptr;
#if defined(QIVEN3_FAULTS)
    memory::Countdown* arenas = nullptr;
#endif
};

// The conditions of the drive check, in their order.
enum class Condition : std::uint8_t { protection, file_history, bitlocker };

namespace detail {

// dir and name joined by the system's separator into out, replaced.
void join(std::string_view dir, std::string_view name, String& out);
// Whether nothing is at path: false for a file, and for one the standard
// library cannot read.
[[nodiscard]] bool absent(std::string_view path);
// The record's bytes for at and dir into out, replaced: PlatformError path,
// naming op, for a dir the record cannot hold (not absolute, a control
// character, or one the strict parser refuses).
void record_bytes(utc::Millis at, std::string_view dir, std::string_view op, String& out);
// The record's bytes read: its dir into dir, replaced; StoreError location
// config, naming op and path, for bytes the record's rules refuse.
void parse_record(std::string_view bytes, std::string_view op, std::string_view path, String& dir);
// change's second rule on the recorded directory old: StoreError location
// not_moved naming the files of v3's it still holds, or old_unreadable
// naming the error; nothing for one gone, its drive included.
void left_behind(std::string_view old, std::string_view op);
// condition's line into out, replaced; true where it is a warning.
bool fact_line(Condition condition, const platform::Reading& reading, std::string_view volume, String& out);
// A typed error's line, as much as fits in out; the view is out's.
std::string_view error_line(const store::StoreError& error, std::span<char> out) noexcept;
std::string_view error_line(const platform::PlatformError& error, std::span<char> out) noexcept;
std::string_view error_line(const memory::MemoryError& error, std::span<char> out) noexcept;
// An exception of the standard library's own (its heap refusing an
// allocation in its file system reads, for one), and its line.
std::string_view error_line(const std::exception& error, std::span<char> out) noexcept;
// The fixed lines.
std::string_view usage_line() noexcept;
std::string_view not_accepted_line() noexcept;
std::string_view moved_line() noexcept;
std::string_view recorded_prefix() noexcept;

// A line's buffer: a StoreError's message and its line end.
inline constexpr std::size_t line_bytes = 1600;

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

// The command argv names, run on platform; its exit status.
template <platform::Platform P>
int main(std::span<const std::string_view> argv, P& platform, const Hooks& hooks = Hooks{}) noexcept {
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

// The store's directory, the recorded dir, into out, on out's arena:
// StoreError location not_chosen where no record is, config where its
// rules refuse it.
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
