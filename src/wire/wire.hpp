#pragma once

// wire: the per-call wire, one process's request and its answer. The
// request is the argument vector after the program's name, each argument
// strict UTF-8, read within bounds its caller gives: how many arguments,
// the bytes of one and the bytes of all. The program's name is the
// system's and is not read. On Windows the binary's wmain gives them as
// UTF-8 converted from the system's UTF-16 by the Windows adapter
// (platform::win::run_utf8), whose refusal, an unpaired surrogate, the
// binary ends with `failed` as well. The answer is bytes written whole to
// standard output and an exit status.
//
// The wire's own failures, a request it cannot read or past a bound and an
// answer it cannot write, end the process with exit status `failed` and no
// answer: read_request gives no request, from which main returns `failed`
// with nothing written; answer gives `failed` where its write fails, after
// which nothing more is written (bytes the system took before the failure
// are the system's). It reads no argument's sense and names no command:
// the commands, their dispatch and their statuses are its callers'.
//
// Budget: nothing allocated; write's and answer's memory is the platform's
// write's. Platform calls: read_request none; write standard_output() and
// its write, once; answer the same where its bytes are not empty, none
// where they are.
//
// thread: none (each call on its caller's thread).

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

namespace qiven3::wire {

// The exit status of the wire's own failures.
inline constexpr int failed = 2;

// A request's bounds, the caller's: the most arguments after the program's
// name, the most bytes in one of them and the most in all of them.
struct Bounds {
    std::size_t count = 0;
    std::size_t argument_bytes = 0;
    std::size_t total_bytes = 0;
};

// A request read: views of the arguments after the program's name, each
// strict UTF-8 and within its bounds, into the argument vector it was read
// from, which outlives it: a View, made and dropped within the main that
// lent the vector.
class RequestView {
public:
    RequestView() noexcept = default;

    [[nodiscard]] std::size_t size() const noexcept { return arguments_.size(); }
    [[nodiscard]] bool empty() const noexcept { return arguments_.empty(); }
    // The bytes of all the arguments.
    [[nodiscard]] std::size_t bytes() const noexcept { return bytes_; }
    // The i-th argument after the program's name; fail_fast for i past
    // size().
    [[nodiscard]] std::string_view operator[](std::size_t i) const noexcept;

private:
    friend std::optional<RequestView> read_request(int argc, const char* const* argv, const Bounds& bounds) noexcept;
    RequestView(std::span<const char* const> arguments, std::size_t bytes) noexcept
        : arguments_(arguments), bytes_(bytes) {}

    std::span<const char* const> arguments_;
    std::size_t bytes_ = 0;
};

// argv's arguments after argv[0] as a request: none where argc is negative,
// argv is null with argc above 0, an argument is null or not strict UTF-8,
// or a bound is passed. An empty vector, argc 0, or the program's name
// alone, is an empty request.
[[nodiscard]] std::optional<RequestView> read_request(int argc, const char* const* argv, const Bounds& bounds) noexcept;

// What the wire writes to: standard output, whose write writes its bytes
// whole or throws. Every platform::Platform is one.
template <class P>
concept Output = requires(P& p, std::string_view bytes) { p.standard_output().write(bytes); };

// bytes written whole to standard output; the platform's error where it
// cannot, which its caller decides.
template <Output P>
void write(P& platform, std::string_view bytes) {
    platform.standard_output().write(bytes);
}

// The answer: bytes written whole to standard output, then status; failed
// where the write throws, whatever it throws. No bytes, no write.
template <Output P>
[[nodiscard]] int answer(P& platform, std::string_view bytes, int status) noexcept {
    if (bytes.empty()) {
        return status;
    }
    try {
        write(platform, bytes);
    } catch (...) {
        return failed;
    }
    return status;
}

}  // namespace qiven3::wire
