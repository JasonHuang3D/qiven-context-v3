// The runner's own cases, in process: each route of run_case and run_main.
// tests/drive/test_runner.py drives the same routes through a process, a
// crash among them.

#include <array>
#include <sstream>
#include <stdexcept>
#include <string>

#include "runner/runner.hpp"

namespace {

using qiven3::test::Case;
using qiven3::test::Context;

// The output of run_case on a case of these bodies, and its status.
struct Ran {
    int status;
    std::string out;
};

Ran run(qiven3::test::CaseFn fn) {
    std::ostringstream out;
    const int status = qiven3::test::run_case(Case{"sample.case", fn, false}, out);
    return Ran{status, out.str()};
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

void sample_counted_held(Context& qiven3_ctx) {
    static_cast<void>(qiven3_ctx.counted.arena(4096).allocate(24, 8));
    static_cast<void>(qiven3_ctx.counted.pool(32, 4096).allocate());
    static_cast<void>(qiven3_ctx.counted.arena(64));
}

}  // namespace

QIVEN3_CASE(runner, pass_status) {
    const Ran r = run(&sample_pass);
    CHECK_EQ(r.status, qiven3::test::exit_pass);
    CHECK_EQ(r.out, std::string("PASS sample.case\n"));
}

QIVEN3_CASE(runner, fail_counts) {
    const Ran r = run(&sample_fail_counts);
    CHECK_EQ(r.status, qiven3::test::exit_fail);
    CHECK(holds(r.out, "): CHECK failed: 1 == 2\n"));
    CHECK(holds(r.out, "): CHECK failed: 2 == 3\n"));
    CHECK(!holds(r.out, "3 == 3"));
    CHECK(holds(r.out, "FAIL sample.case: 2 failed\n"));
}

QIVEN3_CASE(runner, require_stops) {
    reached = false;
    const Ran r = run(&sample_require_stops);
    CHECK_EQ(r.status, qiven3::test::exit_fail);
    CHECK(!reached);
    CHECK(holds(r.out, "stopped by a failed REQUIRE\n"));
    CHECK(holds(r.out, "FAIL sample.case: 1 failed\n"));
}

QIVEN3_CASE(runner, skip_code) {
    CHECK_EQ(qiven3::test::exit_skip, 125);
    const Ran r = run([](Context&) { QIVEN3_SKIP("not applicable: no files here"); });
    CHECK_EQ(r.status, qiven3::test::exit_skip);
    CHECK_EQ(r.out, std::string("SKIP sample.case: not applicable: no files here\n"));
}

QIVEN3_CASE(runner, exception_fails) {
    const Ran r = run([](Context&) { throw std::runtime_error("out of the case"); });
    CHECK_EQ(r.status, qiven3::test::exit_fail);
    CHECK(holds(r.out, "exception: out of the case\n"));
    const Ran other = run([](Context&) { throw 7; });
    CHECK_EQ(other.status, qiven3::test::exit_fail);
    CHECK(holds(other.out, "exception of an unknown type\n"));
}

QIVEN3_CASE(runner, check_eq_shows) {
    const Ran r = run(&sample_check_eq);
    CHECK_EQ(r.status, qiven3::test::exit_fail);
    CHECK(holds(r.out, "): CHECK_EQ failed: std::string(\"ab\") == std::string(\"ac\")\n"));
    CHECK(holds(r.out, "  left:  ab\n  right: ac\n"));
}

QIVEN3_CASE(runner, list_sorted) {
    const std::vector<Case> cases{{"b.x", nullptr, false}, {"a.y", nullptr, true}, {"a.x", nullptr, false}};
    const std::array<std::string_view, 1> args{"--list"};
    std::ostringstream out;
    std::ostringstream err;
    CHECK_EQ(qiven3::test::run_main(args, cases, out, err), qiven3::test::exit_pass);
    CHECK_EQ(out.str(), std::string("a.x\na.y\tserial\nb.x\n"));
    CHECK_EQ(err.str(), std::string());
}

QIVEN3_CASE(runner, run_refusals) {
    const std::vector<Case> cases{{"a.x", nullptr, false}};
    const auto status = [](const std::vector<Case>& in, std::vector<std::string_view> args,
                           std::string& err_text) {
        std::ostringstream out;
        std::ostringstream err;
        const int s = qiven3::test::run_main(args, in, out, err);
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
    const std::vector<Case> twice{{"a.x", nullptr, false}, {"a.x", nullptr, false}};
    CHECK_EQ(status(twice, {"--list"}, err), qiven3::test::exit_usage);
    CHECK_EQ(err, std::string("two cases are named a.x\n"));
    const std::vector<Case> part{{"a.", nullptr, false}};
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
    const Ran r = run(&sample_counted_none_held);
    CHECK_EQ(r.status, qiven3::test::exit_pass);
    CHECK_EQ(r.out, std::string("PASS sample.case\n"));
}

QIVEN3_CASE(runner, counted_held) {
    const Ran r = run(&sample_counted_held);
    CHECK_EQ(r.status, qiven3::test::exit_fail);
    CHECK_EQ(r.out, std::string("held at the case's end: arena 1, 24 bytes\n"
                                "held at the case's end: pool 1, 32 bytes\n"
                                "FAIL sample.case: 2 failed\n"));
}
