#pragma once

// qiven3_runner: the test runner, on the standard library and v3's memory.
// A case is registered by QIVEN3_CASE(group, name) as "group.name"; a case binary
// lists its cases with --list and runs one with --run NAME, the case's
// result its exit status. Each case is its own CTest test. A case takes
// v3's allocators from its context, qiven3_ctx.counted, which counts them
// after it (Counted). Each case run ends with the event case.end in the
// process's log (support/log/journal.hpp), which the runner's main drains
// into the output QIVEN3_LOG names.

#include <array>
#include <cstddef>
#include <deque>
#include <optional>
#include <ostream>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "log/log.hpp"
#include "memory/arena.hpp"
#include "memory/pool.hpp"

namespace qiven3::test {

// The exit statuses of a case binary.
inline constexpr int exit_pass = 0;
inline constexpr int exit_fail = 1;
inline constexpr int exit_usage = 2;
// The suite's skip code: a case that does not apply here. CTest reports it
// as skipped through SKIP_RETURN_CODE; the Python side gives the same code.
inline constexpr int exit_skip = 125;

// The count after each case: the allocators a case takes from its context
// outlive the case, and once it has ended, its values gone, each must hold
// nothing, used() 0, or the case fails, each one holding bytes named with
// its count. Each is over pages of its own, 4,096-byte aligned and zeroed,
// as many bytes as asked.
class Counted {
public:
    Counted() = default;
    Counted(const Counted&) = delete;
    Counted& operator=(const Counted&) = delete;
    ~Counted() = default;

    memory::Arena& arena(std::size_t bytes);
    memory::Pool& pool(std::size_t slot, std::size_t bytes);
    memory::Heap& heap(std::size_t bytes);
    // A line to out for each allocator holding bytes; how many do.
    int holding(std::ostream& out) const;
    // The bytes all of them hold, saturating at SIZE_MAX.
    [[nodiscard]] std::size_t held() const noexcept;

private:
    struct alignas(4096) Page {
        std::array<std::byte, 4096> bytes{};
    };
    template <class A>
    struct Owned {
        explicit Owned(std::size_t bytes) : size(bytes), pages((bytes + sizeof(Page) - 1) / sizeof(Page)) {}
        std::span<std::byte> span() { return std::as_writable_bytes(std::span<Page>(pages)).first(size); }
        std::size_t size;
        std::vector<Page> pages;
        std::optional<A> allocator;  // ended before its pages
    };
    std::deque<Owned<memory::Arena>> arenas_;
    std::deque<Owned<memory::Pool>> pools_;
    std::deque<Owned<memory::Heap>> heaps_;
};

// What a running case reports to: its failed checks, written as they fail,
// the allocators it counts, and the process's log, for the events of its
// cases (fault.injected, support/faults/faults.hpp).
struct Context {
    std::ostream& out;
    log::Log& log;
    int failures = 0;
    Counted counted{};
};

using CaseFn = void (*)(Context&);

struct Case {
    // "group.name", made from the macro's literals.
    log::Name name;
    CaseFn fn = nullptr;
    // A case whose report carries a time runs alone (CTest's RUN_SERIAL).
    bool serial = false;
};

// Every case of this binary, in the order of registration.
std::vector<Case>& registry();

struct Registrar {
    Registrar(log::Name name, CaseFn fn, bool serial);
};

// Thrown by QIVEN3_SKIP: the case does not apply, for the reason given.
struct Skip {
    std::string reason;
};

// Thrown by a failed REQUIRE, after the failure is counted: the case stops.
struct Stop {};

// Counts and reports one check; returns whether it held.
bool check(Context& ctx, bool ok, const char* expr, const char* file, int line);

template <typename T>
concept Printable = requires(std::ostream& o, const T& v) { o << v; };

template <typename T>
std::string show(const T& v) {
    if constexpr (Printable<T>) {
        std::ostringstream s;
        s << v;
        return s.str();
    } else {
        return "(not printable)";
    }
}

template <typename A, typename B>
bool check_eq(Context& ctx, const A& a, const B& b, const char* expr, const char* file, int line) {
    const bool ok = a == b;
    if (!ok) {
        ++ctx.failures;
        ctx.out << file << "(" << line << "): CHECK_EQ failed: " << expr << "\n  left:  "
                << show(a) << "\n  right: " << show(b) << "\n";
        ctx.out.flush();
    }
    return ok;
}

// Runs one case and gives its exit status: exit_pass, exit_fail or
// exit_skip. An exception the case lets out is a failure, as is an
// allocator of its context holding bytes after it; a crash is not caught,
// so the status is the system's. A case that returns, its exception
// caught, ends with case.end in log: its name, its outcome "pass", "fail"
// or "skip", its milliseconds and the bytes its allocators held.
int run_case(const Case& c, std::ostream& out, log::Log& log);

// The runner's command line, with the binary's name dropped:
//   --list        each case's name, sorted, one a line; a serial case's
//                 line adds a tab and "serial"
//   --run NAME    runs that case; its status as run_case gives it
// Anything else, an unknown name, or a registry with an empty or repeated
// name, is exit_usage.
int run_main(std::span<const std::string_view> args, const std::vector<Case>& cases,
             std::ostream& out, std::ostream& err, log::Log& log);

}  // namespace qiven3::test

#define QIVEN3_CAT2(a, b) a##b
#define QIVEN3_CAT(a, b) QIVEN3_CAT2(a, b)

#define QIVEN3_CASE_AS(group, name, serial)                                              \
    static void QIVEN3_CAT(qiven3_case_, QIVEN3_CAT(group, QIVEN3_CAT(_, name)))(        \
        ::qiven3::test::Context & qiven3_ctx);                                           \
    static const ::qiven3::test::Registrar QIVEN3_CAT(                                   \
        qiven3_reg_, QIVEN3_CAT(group, QIVEN3_CAT(_, name))){                            \
        #group "." #name, &QIVEN3_CAT(qiven3_case_, QIVEN3_CAT(group, QIVEN3_CAT(_, name))), \
        serial};                                                                         \
    static void QIVEN3_CAT(qiven3_case_, QIVEN3_CAT(group, QIVEN3_CAT(_, name)))(        \
        [[maybe_unused]] ::qiven3::test::Context & qiven3_ctx)

// QIVEN3_CASE(group, name) { CHECK(...); }
#define QIVEN3_CASE(group, name) QIVEN3_CASE_AS(group, name, false)
// A timed case: CTest runs it with no other test beside it.
#define QIVEN3_SERIAL_CASE(group, name) QIVEN3_CASE_AS(group, name, true)

#define CHECK(cond) \
    ::qiven3::test::check(qiven3_ctx, static_cast<bool>(cond), #cond, __FILE__, __LINE__)
#define CHECK_EQ(a, b) \
    ::qiven3::test::check_eq(qiven3_ctx, (a), (b), #a " == " #b, __FILE__, __LINE__)
#define REQUIRE(cond)                                                                       \
    do {                                                                                    \
        if (!::qiven3::test::check(qiven3_ctx, static_cast<bool>(cond), #cond, __FILE__, \
                                   __LINE__)) {                                             \
            throw ::qiven3::test::Stop{};                                                   \
        }                                                                                   \
    } while (false)
#define QIVEN3_SKIP(reason) throw ::qiven3::test::Skip{std::string(reason)}
