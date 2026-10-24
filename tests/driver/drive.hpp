#pragma once

// The drives the contract tests drive, qiven3-drive-fake and, on Windows
// alone, qiven3-drive-windows: each core instantiated for its one adapter
// (driver/platform_ops.hpp, in its instantiation file), with the HAL's fake.
// Each reads a request a line from its standard input and writes a reply a
// line to its standard output, each one JSON value in canonical form
// (qiven3/json.hpp): keys sorted, no space, integers in decimal within
// plus or minus 2^53 - 1, no float. A request also holds no null and no
// lone surrogate escape, as canonical JSON has none. Bytes cross as
// lowercase hexadecimal strings.
//
//   qiven3-drive-fake --root DIR [--seed N] [--config DIR] [--hal fake]
//                     [--store sqlite|memory]
//   qiven3-drive-windows [--config DIR] [--hal fake]
//
// A request is an object: "op", the operation's name, and its arguments.
// A reply is {"ok":true} with "result" when the operation gives one, or
// {"ok":false,"error":{...}}: "kind" "platform", "memory" or "hal" with
// "code", "operation" and "detail" as the typed error carries them;
// "store" for a store's typed error, with its fields (driver/store_ops.hpp);
// "request" for a request the drive cannot read or an operation it lacks;
// "exception" for any other exception, with its "detail". The drive goes
// on after each.
//
// Each reply also gives "held", the bytes of the platform's pages the
// drive holds after the request (the fake's count of pages lent and not
// given back), and, for a request run on an arena, "memory": its "budget",
// its "peak" and "left", the bytes the arena still held once the
// operation's values were gone, 0 unless a value leaked. A request's
// "budget" replaces the operation's stated one, so a case can plant one a
// byte short. Under QIVEN3_FAULTS a request's "fault", {"k": K, "of":
// "allocation" or "platform"}, fails the operation's K-th allocation on
// its arena or its K-th call of the fake platform, K 0 failing none; the
// reply's "fault" gives the calls counted and whether the K-th came.
// Built without it, "fault" is refused, "not applicable:".
//
// Each request is an operation of the drive's log (support/log/journal.hpp),
// named from the drive's table of operations, else "unknown":
// drive.request with its bytes, error.typed for a typed error, drive.reply
// with its status ("ok" or the error's kind) and bytes, and op.end with its
// outcome ("ok", the typed error's code, or the kind) and its arena's peak;
// the log is drained after each reply into the output QIVEN3_LOG names. No
// field holds a request's text.
//
// The adapter is made at its first platform operation, so an adapter that
// cannot be made is a reply. --config overrides the configuration
// location; the fake's is otherwise "<root>/qiven3". --hal fake is refused
// by a build with QIVEN3_HAL off, which has no HAL operation. --store, the
// fake's drive alone, selects the store its store requests make, under
// "<root>/stores" (driver/store_ops.hpp); without it there are none.
//
// This header is the drive's common half, no adapter's: its options, the
// session's state (the HAL's, driver/hal_ops.hpp, in a build with it), the
// request helpers, adapter_for and current as a command's main calls them,
// and the HAL's operations.

#include <cstddef>
#include <cstdint>
#include <concepts>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "log/catalogue.hpp"
#include "log/log.hpp"
#include "memory/arena.hpp"
#include "platform/platform.hpp"
#include "qiven3/json.hpp"
#include "support/log/journal.hpp"

#if defined(QIVEN3_DRIVE_HAL)
#include "driver/hal_ops.hpp"
#endif

namespace qiven3::drive {

struct Options {
    std::optional<std::string> root, config, store;
    std::uint64_t seed = 0;
    bool hal_fake = false;
};

// The arguments; none for any other, `why` saying so. `fake` takes --root,
// --seed and --store, which the Windows drive refuses.
std::optional<Options> read_options(int argc, char** argv, bool fake, std::string& why);
// Arguments refused: `program: why` on standard error; the exit status, 2.
int refuse(std::string_view program, const std::string& why);

// The stated budget of each operation of the platform's and the HAL's
// members, host side; read adds the file's size, run its caps.
inline constexpr std::size_t member_budget = std::size_t{64} << 10;

// One request's arena: its budget, its peak and the bytes it still held
// once the operation's values were gone. None for a request on no arena.
struct Metered {
    bool counted = false;
    std::size_t budget = 0, peak = 0, left = 0;
};

#if defined(QIVEN3_FAULTS)
// The countdowns a request's "fault" arms: its arena's allocations, or the
// fake platform's calls. thread: one.
struct Faults {
    memory::Countdown allocation, platform;
    memory::Countdown* armed = nullptr;
    std::size_t k = 0;
};
#endif

// The state the drive's common operations keep: its options, the request's
// record and, in a build with the HAL, the HAL's.
class Session {
public:
    explicit Session(Options options) : options_(std::move(options)) {}
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    ~Session() = default;

    const Options& options() const noexcept { return options_; }
    Metered& metered() noexcept { return metered_; }
#if defined(QIVEN3_FAULTS)
    Faults& faults() noexcept { return faults_; }
#endif
#if defined(QIVEN3_DRIVE_HAL)
    Hal& hal() noexcept { return hal_; }
#endif

private:
    Options options_;
    Metered metered_;
#if defined(QIVEN3_FAULTS)
    Faults faults_;
#endif
#if defined(QIVEN3_DRIVE_HAL)
    Hal hal_;
#endif
};

// A request the drive cannot serve: the reply's kind "request".
class RequestError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// A request, and a reply: json's value, on the standard allocator, since
// the drive's own values are no operation's and stay off its arena.
using Value = json::Basic<std::allocator<char>>;

// A request, or one of its nodes: the request and an index in it, made and
// dropped within the request's call. Each read of another kind throws
// RequestError naming what.
class ArgView {
public:
    ArgView(const Value& value, json::Index at = 0) noexcept : value_(&value), at_(at) {}

    [[nodiscard]] json::Kind kind() const { return value_->kind(at_); }
    // An object's member, or none, as for a node of another kind.
    [[nodiscard]] std::optional<ArgView> find(std::string_view key) const;
    [[nodiscard]] bool as_boolean(std::string_view what) const;
    [[nodiscard]] std::int64_t as_integer(std::string_view what) const;
    [[nodiscard]] std::string as_string(std::string_view what) const;
    [[nodiscard]] std::vector<ArgView> as_array(std::string_view what) const;
    // An object's members, key and value, in the request's order.
    [[nodiscard]] std::vector<std::pair<std::string, ArgView>> as_object(std::string_view what) const;

private:
    void expect(json::Kind kind, std::string_view what, const char* name) const;

    const Value* value_;
    json::Index at_;
};

// The line read as one request: RequestError for a line that is no
// canonical value's text, as json reads it, or that holds a null or a
// lone surrogate escape.
Value parse(std::string_view line);
// A reply's canonical bytes.
std::string encode(const Value& reply);

// A reply's parts: a string, an integer or a flag alone, and an empty
// array or object, which set and push fill.
Value scalar(std::string_view text);
Value scalar(std::int64_t integer);
template <std::same_as<bool> B>
Value scalar(B flag) {
    Value value;
    json::Writer<std::allocator<char>>(value).boolean(flag);
    return value;
}
Value array();
Value object();

// Bytes as lowercase hexadecimal, and back: RequestError for an odd length
// or a character that is no hexadecimal digit.
std::string to_hex(std::string_view bytes);
std::string from_hex(std::string_view hex);

// The request's argument: RequestError when it is absent.
ArgView argument(ArgView request, std::string_view key);
// The argument as text, an integer, a flag, or its default when absent.
std::string text(ArgView request, std::string_view key);
std::int64_t integer(ArgView request, std::string_view key, std::optional<std::int64_t> fallback = {});
bool flag(ArgView request, std::string_view key, bool fallback);
// An integer argument of milliseconds, 5000 when absent, as Seconds.
platform::Seconds deadline(ArgView request, std::string_view key);

// The request's budget: its "budget" when given, else the stated one.
std::size_t budget(ArgView request, std::size_t stated);

// Arms the countdown the request's "fault" names, or none; RequestError
// for a fault built without QIVEN3_FAULTS. The record is cleared.
void begin(Session& session, ArgView request);
// The reply with the request's record, its fault's and `held`; every
// countdown disarmed.
Value finish(Session& session, Value reply, std::size_t held);

// f(arena) on an arena over `pages`, the operation's budget: the arena's
// peak and what it held after f's values were gone go to the session's
// record, whether f returns or throws. Under QIVEN3_FAULTS the arena asks
// the allocation countdown.
template <class F>
std::optional<Value> on_arena(Session& session, std::span<std::byte> pages, F&& f) {
    memory::Arena arena(pages);
#if defined(QIVEN3_FAULTS)
    arena.attach(&session.faults().allocation);
#endif
    struct Note {
        Note(Metered& m, memory::Arena& a) noexcept : metered(m), arena(a) {}
        Note(const Note&) = delete;
        Note& operator=(const Note&) = delete;
        ~Note() {
            metered.counted = true;
            metered.budget = arena.budget();
            metered.peak = arena.peak();
            metered.left = arena.used();
        }
        Metered& metered;
        memory::Arena& arena;
    };
    const Note note{session.metered(), arena};
    return f(arena);
}

// The common operations: adapter_for and current, and the HAL's.
using Operation = std::optional<Value> (*)(Session&, ArgView request);
using Operations = std::map<std::string, Operation, std::less<>>;
void add_hal_operations(Operations& into);  // a build with the HAL alone
const Operations& operations();

// The common operation's result; RequestError for a name none has.
std::optional<Value> common(Session& session, const std::string& name, ArgView request);

// The replies.
Value ok_reply(std::optional<Value> result);
Value error_reply(Value error);
Value typed(std::string_view kind, std::string_view code, std::string_view operation, std::string_view detail);
Value plain(std::string_view kind, std::string_view detail);
// The reply to the exception in flight, the operation `name`'s.
Value caught(std::string_view name);

// The request loop's halves: the streams made ready, once; the next line
// of standard input, false at its end; a reply written as a line, its
// bytes given back.
void ready_streams();
bool next_line(std::string& line);
std::size_t send(const Value& reply);

// The log's halves: the pages of the process's Journal; a request's name,
// its "op" picked from the drive's names, else "unknown", as for no request;
// and a reply's events, op ended.
test::Journal::Pages& journal_pages() noexcept;
log::Name named(const std::optional<Value>& request) noexcept;
void noted(log::Op& op, const Value& reply, std::size_t bytes) noexcept;

// The request loop over standard input and output, each line answered by
// drive.reply(request); the process's exit status.
template <class Drive>
int loop(Drive& drive) {
    ready_streams();
    test::Journal journal(journal_pages(), "drive");
    std::string line;
    while (next_line(line)) {
        std::optional<Value> request;
        Value answer;
        try {
            request.emplace(parse(line));
        } catch (const RequestError& error) {
            answer = error_reply(plain("request", error.what()));
        }
        const log::Name name = named(request);
        log::Op op = log::begin(journal.log(), name);
        log::event(op, log::events::drive_request, name, log::Count(line.size()));
        if (request) {
            try {
                answer = drive.reply(*request);
            } catch (const RequestError& error) {
                answer = error_reply(plain("request", error.what()));
            }
        }
        noted(op, answer, send(answer));
        journal.drain();
    }
    return 0;
}

}  // namespace qiven3::drive
