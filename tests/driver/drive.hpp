#pragma once

// The drives the contract tests drive, qiven3-drive-fake and, on Windows
// alone, qiven3-drive-windows: each core instantiated for its one adapter
// (driver/platform_ops.hpp, in its instantiation file), with the HAL's fake.
// Each reads a request a line from its standard input and writes a reply a
// line to its standard output (driver/wire.hpp).
//
//   qiven3-drive-fake --root DIR [--seed N] [--config DIR] [--hal fake]
//   qiven3-drive-windows [--config DIR] [--hal fake]
//
// A request is an object: "op", the operation's name, and its arguments.
// A reply is {"ok":true} with "result" when the operation gives one, or
// {"ok":false,"error":{...}}: "kind" "platform", "memory" or "hal" with
// "code", "operation" and "detail" as the typed error carries them;
// "request" for a request the drive cannot read or an operation it lacks;
// "exception" for any other exception, with its "detail". The drive goes
// on after each.
//
// The adapter is made at its first platform operation, so an adapter that
// cannot be made is a reply. --config overrides the configuration
// location; the fake's is otherwise "<root>/qiven3". --hal fake is refused
// by a build with QIVEN3_HAL off, which has no HAL operation.
//
// This header is the drive's common half, no adapter's: its options, the
// session's state, the request helpers, adapter_for and current as a
// command's main calls them, and the HAL's operations.

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include "driver/wire.hpp"
#include "platform/platform.hpp"

namespace qiven3::drive {

struct Options {
    std::optional<std::string> root, config;
    std::uint64_t seed = 0;
    bool hal_fake = false;
};

// The arguments; none for any other, `why` saying so. `fake` takes --root
// and --seed, which the Windows drive refuses.
std::optional<Options> read_options(int argc, char** argv, bool fake, std::string& why);

// The state the drive's common operations keep: its options, and state an
// operation module keeps by its own key (the HAL's).
class Session {
public:
    explicit Session(Options options) : options_(std::move(options)) {}
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    ~Session() = default;

    const Options& options() const noexcept { return options_; }
    std::shared_ptr<void>& state(const std::string& key) { return state_[key]; }

private:
    Options options_;
    std::map<std::string, std::shared_ptr<void>> state_;
};

// A request the drive cannot serve: the reply's kind "request".
class RequestError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// The request's argument: RequestError when it is absent.
const Value& argument(const Value& request, std::string_view key);
// The argument as text, an integer, a flag, or its default when absent.
std::string text(const Value& request, std::string_view key);
std::int64_t integer(const Value& request, std::string_view key, std::optional<std::int64_t> fallback = {});
bool flag(const Value& request, std::string_view key, bool fallback);
// An integer argument of milliseconds, 5000 when absent, as Seconds.
platform::Seconds deadline(const Value& request, std::string_view key);

// The common operations: adapter_for and current, and the HAL's.
using Operation = std::function<std::optional<Value>(Session&, const Value& request)>;
using Operations = std::map<std::string, Operation, std::less<>>;
void add_hal_operations(Operations& into);  // a build with the HAL alone
const Operations& operations();

// The common operation's result; RequestError for a name none has.
std::optional<Value> common(Session& session, const std::string& name, const Value& request);

// The replies.
Value ok_reply(std::optional<Value> result);
Value error_reply(Value error);
Value typed(std::string_view kind, std::string_view code, std::string_view operation, std::string_view detail);
Value plain(std::string_view kind, std::string_view detail);
// The reply to the exception in flight, the operation `name`'s.
Value caught(std::string_view name);

// The request loop over standard input and output, each line answered by
// `reply`; the process's exit status.
int loop(const std::function<Value(const Value&)>& reply);

}  // namespace qiven3::drive
