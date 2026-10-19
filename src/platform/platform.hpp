#pragma once

// The platform contract: every platform fact v3 depends on sits behind it,
// and core logic depends on this header alone of src/platform/. Core takes
// a Platform& passed in, never a global; only a binary's main chooses the
// adapter (platform/current.hpp).
//
// Paths and text cross the seam as UTF-8 std::string, bytes as std::string
// too. A caller passes the defaults itself: false for config_dir's create,
// 5 s for each deadline.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace qiven3::platform {

using Seconds = std::chrono::duration<double>;

// locked is BitLocker's alone.
enum class State { on, off, locked, unknown };

struct Reading {
    State state;
    std::string reason;
};

struct Facts {
    std::string volume;
    Reading protection, file_history, bitlocker;
};

struct Caps {
    std::uint64_t out, err;
};

struct Command {
    std::vector<std::string> argv;
    std::string cwd;
    std::vector<std::pair<std::string, std::string>> env;
    Seconds deadline;
    Caps caps;
};

struct Ran {
    int code;
    std::string out, err;
};

struct Row {
    std::string name, applies, kept, how_long, owner_can;
};

// A file written as a stream; its destructor closes as close() does.
class Sink {
public:
    virtual ~Sink() = default;
    virtual void write(std::string_view bytes) = 0;
    virtual void close() = 0;
};

// A staging directory; its destructor discards it if it is unpublished.
class Staged {
public:
    virtual ~Staged() = default;
    virtual std::unique_ptr<Sink> open(const std::string& path) = 0;
    virtual void publish(const std::string& name, Seconds deadline) = 0;
    virtual void discard() = 0;
};

// A held lock; its destructor releases it.
class Lock {
public:
    virtual ~Lock() = default;
    virtual void release() = 0;
};

// A started process tree; its destructor kills the tree.
class Child {
public:
    virtual ~Child() = default;
    virtual std::optional<std::string> read_line(Seconds timeout) = 0;
    virtual void write(std::string_view bytes) = 0;
    virtual void close_input() = 0;
    virtual int wait(Seconds timeout) = 0;
    virtual void kill() = 0;
};

// v3's standard output: UTF-8 at the seam, a stream that never fails to
// encode.
class Output {
public:
    virtual ~Output() = default;
    virtual void write(std::string_view utf8) = 0;
};

class Platform {
public:
    virtual ~Platform() = default;
    // "windows" or "fake".
    virtual std::string name() const = 0;
    // The configuration location, an absolute path, made first if create:
    // config, exists, io.
    virtual std::string config_dir(bool create) = 0;
    // An absolute path's one form: path.
    virtual std::string resolve(const std::string& path) = 0;
    // A resolved path's volume and its three readings: path.
    virtual Facts facts(const std::string& path) = 0;
    // The sync client's name for a resolved path, or none: path.
    virtual std::optional<std::string> synced(const std::string& path) = 0;
    // A file's new bytes, durably, through a temporary file: busy, io.
    virtual void replace(const std::string& path, std::string_view data, Seconds deadline) = 0;
    // A staging directory in an existing directory: path, exists, busy, io.
    virtual std::unique_ptr<Staged> stage(const std::string& directory) = 0;
    // An empty file, or a directory, made only if absent: path, exists, io.
    virtual void create(const std::string& path, bool directory) = 0;
    // A file's bytes, whole: path, io.
    virtual std::string read(const std::string& path) = 0;
    // A lock file's first byte, held exclusively: path, busy, io.
    virtual std::unique_ptr<Lock> lock(const std::string& path, Seconds deadline) = 0;
    // A program's absolute path: start.
    virtual std::string program(const std::string& name) = 0;
    virtual std::unique_ptr<Child> start(const Command& command) = 0;
    // start, deadline, output.
    virtual Ran run(const Command& command, std::string_view data) = 0;
    // The console's code page, cp<n>.
    virtual std::string code_page() = 0;
    // The text's stdout(), named so since stdout is a C macro.
    virtual Output& standard_output() = 0;
    // The platform's residual rows for the caller's absolute paths: path.
    virtual std::vector<Row> residual(const std::vector<std::string>& paths) = 0;
    // n bytes from the system's generator: io.
    virtual std::string random_bytes(std::size_t n) = 0;
    // UTC, milliseconds since 1970, truncated.
    virtual std::chrono::sys_time<std::chrono::milliseconds> now() = 0;
};

enum class Code { unsupported, config, path, exists, busy, io, start, deadline, output };

// The code's name, as the text spells it: "unsupported", "config", ...
std::string_view code_name(Code code) noexcept;

// A platform operation's typed error. detail names the system's error that
// caused it, by name and number, where one did. It never crosses a process
// boundary: a command's main maps it to its exit status and output.
class PlatformError : public std::runtime_error {
public:
    PlatformError(Code code, std::string operation, std::string detail);
    Code code() const noexcept { return code_; }
    const std::string& operation() const noexcept { return operation_; }
    const std::string& detail() const noexcept { return detail_; }

private:
    Code code_;
    std::string operation_;
    std::string detail_;
};

}  // namespace qiven3::platform
