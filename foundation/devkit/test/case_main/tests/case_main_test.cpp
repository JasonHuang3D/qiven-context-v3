// The case main's own cases: case.end's line, written by its Ended, and the
// name of a platform error a case lets out, each read through a Log of the
// case's own.

#include <sstream>
#include <string>

#include "qiven/case_main/ended.hpp"
#include "qiven/case_main/journal.hpp"
#include "qiven/log/log.hpp"
#include "qiven/platform/platform.hpp"
#include "qiven/runner/runner.hpp"

namespace {

using qiven::runner::Case;
using qiven::runner::Context;

// The Name of the case run() runs, which its case.end carries.
constexpr qiven::log::Name sample_case = "sample.case";

// The output of run_case on a case of this body, its status, and the lines
// of its Log, numbered 2, over an arena of its context.
struct Ran {
    int status;
    std::string out;
    std::string lines;
};

Ran run(Context& ctx, qiven::runner::CaseFn fn) {
    std::ostringstream out;
    qiven::log::Log log(ctx.counted.arena(qiven::test::Journal::bytes), qiven::log::Count(2U), "case-main-test");
    const int status =
        qiven::runner::run_case(Case{sample_case.text(), &sample_case, fn}, out, log, &qiven::test::case_ended);
    const auto view = log.ring().since(log.ring().first());
    std::string lines(view.older);
    lines.append(view.newer);
    return Ran{status, out.str(), lines};
}

bool holds(const std::string& text, const char* part) {
    return text.find(part) != std::string::npos;
}

// The sample cases' bodies, at namespace scope: a lambda in a case taking its
// own qiven_ctx would shadow the case's.
void sample_pass(Context& qiven_ctx) { CHECK(1 + 1 == 2); }
void sample_fail(Context& qiven_ctx) { CHECK(1 == 2); }
void sample_held(Context& qiven_ctx) {
    static_cast<void>(qiven_ctx.counted.arena(4096).allocate(24, 8));
    static_cast<void>(qiven_ctx.counted.arena(64));
}

}  // namespace

// case.end, its fields in the key order json writes them; ms the case's.
QIVEN_CASE(case_main, case_end) {
    const Ran pass = run(qiven_ctx, &sample_pass);
    CHECK(holds(pass.lines, R"({"case":"sample.case","event":"case.end","held":0,"level":"info","log":2,"ms":)"));
    CHECK(holds(pass.lines, R"(,"op":0,"outcome":"pass","t":)"));
    CHECK(holds(run(qiven_ctx, &sample_fail).lines, R"("outcome":"fail")"));
    CHECK(holds(run(qiven_ctx, [](Context&) { QIVEN_SKIP("not applicable: here"); }).lines, R"("outcome":"skip")"));
    CHECK(holds(run(qiven_ctx, [](Context&) { throw 7; }).lines, R"("outcome":"fail")"));
    const Ran held = run(qiven_ctx, &sample_held);
    CHECK(holds(held.lines, R"("held":24,)"));
    CHECK(holds(held.lines, R"("outcome":"fail")"));
}

// The platform error's namer, the main's: its code's name and its detail.
QIVEN_CASE(case_main, platform_error_named) {
    const Ran r = run(qiven_ctx, [](Context&) {
        throw qiven::platform::PlatformError(qiven::platform::Code::busy, "replace", "the target is held");
    });
    CHECK_EQ(r.status, qiven::runner::exit_fail);
    CHECK(holds(r.out, "platform error busy: the target is held\n"));
    CHECK(!holds(r.out, "unknown type"));
    CHECK(holds(r.out, "FAIL sample.case: 1 failed\n"));
    CHECK(holds(r.lines, R"("outcome":"fail")"));
}
