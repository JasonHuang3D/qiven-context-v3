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
#include "store/contract.hpp"

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

}  // namespace detail

// The command argv names, run on platform; its exit status. This header
// declares location's templates on the platform; qiven3/location_impl.hpp
// defines them, and instantiation files alone include it.
template <platform::Platform P>
int main(std::span<const std::string_view> argv, P& platform, const Hooks& hooks = Hooks{}) noexcept;

// The store's directory, the recorded dir, into out, on out's arena:
// StoreError location not_chosen where no record is, config where its
// rules refuse it.
template <platform::Platform P>
void store_dir(P& platform, String& out);

}  // namespace qiven3::location
