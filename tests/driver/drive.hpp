#pragma once

// qiven3-drive: the binary the contract tests drive. Its arguments choose
// the adapter and the HAL; it reads a request a line from its standard
// input and writes a reply a line to its standard output (driver/wire.hpp).
//
//   qiven3-drive --platform fake --root DIR [--seed N] [--config DIR] [--hal fake]
//   qiven3-drive --platform windows [--config DIR] [--hal fake]
//
// A request is an object: "op", the operation's name, and its arguments.
// A reply is {"ok":true} with "result" when the operation gives one, or
// {"ok":false,"error":{...}}: "kind" "platform" or "hal" with "code",
// "operation" and "detail" as the typed error carries them; "request" for
// a request the drive cannot read or an operation it lacks; "exception"
// for any other exception, with its "detail". The drive goes on after each.
//
// The adapter is made at its first platform operation, so an adapter that
// cannot be made is a reply. --config overrides the configuration
// location; the fake's is otherwise "<root>/qiven3". --hal fake is refused
// by a build with QIVEN3_HAL off, which has no HAL operation.

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "driver/wire.hpp"
#include "platform/platform.hpp"

namespace qiven3::platform::fake {
class FakePlatform;
}

namespace qiven3::drive {

struct Options {
    std::string platform;  // "fake" or "windows"
    std::optional<std::string> root, config;
    std::uint64_t seed = 0;
    bool hal_fake = false;
};

class Session;

// An operation: the request in, the result out, or none.
using Operation = std::function<std::optional<Value>(Session&, const Value& request)>;

class Session {
public:
    explicit Session(Options options);
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    const Options& options() const noexcept { return options_; }
    // The chosen adapter, made at the first call.
    platform::Platform& platform();
    // The fake when it is the chosen adapter; else a request error naming
    // the operation, the fake's alone.
    platform::fake::FakePlatform& fake(std::string_view operation);
    // State an operation module keeps, by its own key.
    std::shared_ptr<void>& state(const std::string& key) { return state_[key]; }

    // One request's reply, never throwing.
    Value reply(const Value& request);

private:
    Options options_;
    std::unique_ptr<platform::Platform> platform_;
    platform::fake::FakePlatform* fake_ = nullptr;
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

using Operations = std::map<std::string, Operation, std::less<>>;
void add_platform_operations(Operations& into);
void add_hal_operations(Operations& into);  // a build with the HAL alone
const Operations& operations();

}  // namespace qiven3::drive
