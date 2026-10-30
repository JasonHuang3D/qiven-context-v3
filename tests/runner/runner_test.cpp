// The runner's own cases, in process: each route of run_case and run_main.
// tests/drive/test_runner.py drives the same routes through a process, a
// crash among them. Each run's case.end is read back from a Log of the
// case's own.

#include <array>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <string>

#include "log/log.hpp"
#include "memory/memory.hpp"
#include "platform/platform.hpp"
#include "runner/runner.hpp"
#include "support/log/cases.hpp"
#include "support/log/journal.hpp"

namespace {

using qiven3::test::Case;
using qiven3::test::Context;

// A case of name, serial or not, of body fn, with no Name of the log's.
Case sample(std::string_view name, bool serial, qiven3::test::CaseFn fn = nullptr) {
    return Case{name, nullptr, fn, serial};
}

// The Name of the case run() runs, which its case.end carries.
constexpr qiven3::log::Name sample_case = "sample.case";

// A Log of a case's own, numbered 2, over an arena of its context.
class Logged {
public:
    explicit Logged(Context& ctx)
        : log(ctx.counted.arena(qiven3::test::Journal::bytes), qiven3::log::Count(2U), "runner-test") {}
    // Its lines, oldest first.
    [[nodiscard]] qiven3::test::Lines lines() const { return qiven3::test::Lines(log.ring().since(log.ring().first())); }
    qiven3::log::Log log;
};

// The output of run_case on a case of these bodies, its status, and its
// log's last line.
struct Ran {
    int status;
    std::string out;
    std::string event;
};

Ran run(Context& ctx, qiven3::test::CaseFn fn) {
    std::ostringstream out;
    Logged logged(ctx);
    const int status = qiven3::test::run_case(Case{sample_case.text(), &sample_case, fn, false}, out, logged.log,
                                              &qiven3::test::case_ended);
    const qiven3::test::Lines lines = logged.lines();
    return Ran{status, out.str(), lines.at(lines.size() - 1)};
}

bool reached = false;

bool holds(const std::string& text, const char* part) {
    return text.find(part) != std::string::npos;
}

// The sample cases' bodies, at namespace scope: a lambda in a case taking its
// own qiven3_ctx would shadow the case's.
void sample_pass(Context& qiven3_ctx) { CHECK(1 + 1 == 2); }

void sample_fail_counts(Context& qiven3_ctx) {
    CHECK(1 == 2);
    CHECK(2 == 3);
    CHECK(3 == 3);
}

void sample_require_stops(Context& qiven3_ctx) {
    REQUIRE(false);
    reached = true;
}

void sample_check_eq(Context& qiven3_ctx) { CHECK_EQ(std::string("ab"), std::string("ac")); }

void sample_counted_none_held(Context& qiven3_ctx) {
    qiven3::memory::Arena& arena = qiven3_ctx.counted.arena(4096);
    const qiven3::memory::String text("a text past the string's own small buffer",
                                      qiven3::memory::Allocator<char>(arena));
    CHECK(arena.used() > 0);
    qiven3::memory::Pool& pool = qiven3_ctx.counted.pool(32, 4096);
    pool.free(pool.allocate());
    qiven3::memory::Heap& heap = qiven3_ctx.counted.heap(8192);
    heap.free(heap.allocate(100));
}

// The pool slots sample_counted_held holds, the last first, each holding
// the one before it in its first bytes. Under QIVEN3_SYSTEM_HEAP a slot is
// the system heap's: held from here, it stays reachable, and LeakSanitizer
// reports no leak of it, while any other block lost still is one. The
// runner's count is what reads the bytes as held.
void* held_slots = nullptr;

void sample_counted_held(Context& qiven3_ctx) {
    static_cast<void>(qiven3_ctx.counted.arena(4096).allocate(24, 8));
    void* slot = qiven3_ctx.counted.pool(32, 4096).allocate();
    std::memcpy(slot, static_cast<const void*>(&held_slots), sizeof held_slots);
    held_slots = slot;
    static_cast<void>(qiven3_ctx.counted.arena(64));
}

}  // namespace

QIVEN3_CASE(runner, pass_status) {
    const Ran r = run(qiven3_ctx, &sample_pass);
    CHECK_EQ(r.status, qiven3::test::exit_pass);
    CHECK_EQ(r.out, std::string("PASS sample.case\n"));
}

// case.end, its fields in the key order json writes them; ms the case's.
QIVEN3_CASE(runner, case_end) {
    const Ran pass = run(qiven3_ctx, &sample_pass);
    CHECK(holds(pass.event, R"({"case":"sample.case","event":"case.end","held":0,"level":"info","log":2,"ms":)"));
    CHECK(holds(pass.event, R"(,"op":0,"outcome":"pass","t":)"));
    const Ran fail = run(qiven3_ctx, &sample_fail_counts);
    CHECK(holds(fail.event, R"("outcome":"fail")"));
    const Ran skip = run(qiven3_ctx, [](Context&) { QIVEN3_SKIP("not applicable: here"); });
    CHECK(holds(skip.event, R"("outcome":"skip")"));
    const Ran thrown = run(qiven3_ctx, [](Context&) { throw 7; });
    CHECK(holds(thrown.event, R"("outcome":"fail")"));
    // The bytes held: the arena's 24 and the pool's 32.
    const Ran held = run(qiven3_ctx, &sample_counted_held);
    CHECK(holds(held.event, R"("held":56,)"));
    CHECK(holds(held.event, R"("outcome":"fail")"));
}

QIVEN3_CASE(runner, fail_counts) {
    const Ran r = run(qiven3_ctx, &sample_fail_counts);
    CHECK_EQ(r.status, qiven3::test::exit_fail);
    CHECK(holds(r.out, "): CHECK failed: 1 == 2\n"));
    CHECK(holds(r.out, "): CHECK failed: 2 == 3\n"));
    CHECK(!holds(r.out, "3 == 3"));
    CHECK(holds(r.out, "FAIL sample.case: 2 failed\n"));
}

QIVEN3_CASE(runner, require_stops) {
    reached = false;
    const Ran r = run(qiven3_ctx, &sample_require_stops);
    CHECK_EQ(r.status, qiven3::test::exit_fail);
    CHECK(!reached);
    CHECK(holds(r.out, "stopped by a failed REQUIRE\n"));
    CHECK(holds(r.out, "FAIL sample.case: 1 failed\n"));
}

QIVEN3_CASE(runner, skip_code) {
    CHECK_EQ(qiven3::test::exit_skip, 125);
    const Ran r = run(qiven3_ctx, [](Context&) { QIVEN3_SKIP("not applicable: no files here"); });
    CHECK_EQ(r.status, qiven3::test::exit_skip);
    CHECK_EQ(r.out, std::string("SKIP sample.case: not applicable: no files here\n"));
}

QIVEN3_CASE(runner, exception_fails) {
    const Ran r = run(qiven3_ctx, [](Context&) { throw std::runtime_error("out of the case"); });
    CHECK_EQ(r.status, qiven3::test::exit_fail);
    CHECK(holds(r.out, "exception: out of the case\n"));
    const Ran other = run(qiven3_ctx, [](Context&) { throw 7; });
    CHECK_EQ(other.status, qiven3::test::exit_fail);
    CHECK(holds(other.out, "exception of an unknown type\n"));
}

QIVEN3_CASE(runner, platform_error_named) {
    const Ran r = run(qiven3_ctx, [](Context&) {
        throw qiven3::platform::PlatformError(qiven3::platform::Code::busy, "replace", "the target is held");
    });
    CHECK_EQ(r.status, qiven3::test::exit_fail);
    CHECK(holds(r.out, "platform error busy: the target is held\n"));
    CHECK(!holds(r.out, "unknown type"));
    CHECK(holds(r.out, "FAIL sample.case: 1 failed\n"));
    CHECK(holds(r.event, R"("outcome":"fail")"));
}

QIVEN3_CASE(runner, memory_error_named) {
    // A MemoryError has no base: its code's name, its operation and its
    // detail are named, and the case is counted failed.
    const Ran r = run(qiven3_ctx, [](Context&) { throw qiven3::memory::MemoryError::budget("arena", 64, 80, 16); });
    CHECK_EQ(r.status, qiven3::test::exit_fail);
    CHECK(holds(r.out, "memory error budget in arena: budget 64, request 80, held 16\n"));
    CHECK(!holds(r.out, "unknown type"));
    CHECK(holds(r.out, "FAIL sample.case: 1 failed\n"));
    CHECK(holds(r.event, R"("outcome":"fail")"));
}

QIVEN3_CASE(runner, check_eq_shows) {
    const Ran r = run(qiven3_ctx, &sample_check_eq);
    CHECK_EQ(r.status, qiven3::test::exit_fail);
    CHECK(holds(r.out, "): CHECK_EQ failed: std::string(\"ab\") == std::string(\"ac\")\n"));
    CHECK(holds(r.out, "  left:  ab\n  right: ac\n"));
}

QIVEN3_CASE(runner, list_sorted) {
    const std::vector<Case> cases{sample("b.x", false), sample("a.y", true), sample("a.x", false)};
    const std::array<std::string_view, 1> args{"--list"};
    std::ostringstream out;
    std::ostringstream err;
    Logged logged(qiven3_ctx);
    CHECK_EQ(qiven3::test::run_main(args, cases, out, err, logged.log), qiven3::test::exit_pass);
    CHECK_EQ(out.str(), std::string("a.x\na.y\tserial\nb.x\n"));
    CHECK_EQ(err.str(), std::string());
}

QIVEN3_CASE(runner, run_refusals) {
    const std::vector<Case> cases{sample("a.x", false)};
    Logged logged(qiven3_ctx);
    const auto status = [&logged](const std::vector<Case>& in, std::vector<std::string_view> args,
                                  std::string& err_text) {
        std::ostringstream out;
        std::ostringstream err;
        const int s = qiven3::test::run_main(args, in, out, err, logged.log);
        err_text = err.str();
        return s;
    };
    std::string err;
    CHECK_EQ(status(cases, {"--run", "a.y"}, err), qiven3::test::exit_usage);
    CHECK_EQ(err, std::string("no such case: a.y\n"));
    CHECK_EQ(status(cases, {}, err), qiven3::test::exit_usage);
    CHECK_EQ(err, std::string("usage: --list | --run NAME\n"));
    CHECK_EQ(status(cases, {"--run"}, err), qiven3::test::exit_usage);
    CHECK_EQ(status(cases, {"--list", "a.x"}, err), qiven3::test::exit_usage);
    CHECK_EQ(status(cases, {"--run", "a.x", "a.x"}, err), qiven3::test::exit_usage);
    const std::vector<Case> twice{sample("a.x", false), sample("a.x", false)};
    CHECK_EQ(status(twice, {"--list"}, err), qiven3::test::exit_usage);
    CHECK_EQ(err, std::string("two cases are named a.x\n"));
    const std::vector<Case> part{sample("a.", false)};
    CHECK_EQ(status(part, {"--list"}, err), qiven3::test::exit_usage);
    CHECK_EQ(err, std::string("a case has an incomplete name: 'a.'\n"));
}

QIVEN3_SERIAL_CASE(runner, registry_holds_self) {
    bool found = false;
    for (const Case& c : qiven3::test::registry()) {
        if (c.name == "runner.registry_holds_self") {
            found = c.serial;
        }
    }
    CHECK(found);
}

QIVEN3_CASE(runner, counted_none_held) {
    const Ran r = run(qiven3_ctx, &sample_counted_none_held);
    CHECK_EQ(r.status, qiven3::test::exit_pass);
    CHECK_EQ(r.out, std::string("PASS sample.case\n"));
}

QIVEN3_CASE(runner, counted_held) {
    const void* const before = held_slots;
    const Ran r = run(qiven3_ctx, &sample_counted_held);
    CHECK(held_slots != nullptr && held_slots != before);
    CHECK_EQ(r.status, qiven3::test::exit_fail);
    CHECK_EQ(r.out, std::string("held at the case's end: arena 1, 24 bytes\n"
                                "held at the case's end: pool 1, 32 bytes\n"
                                "FAIL sample.case: 2 failed\n"));
}
