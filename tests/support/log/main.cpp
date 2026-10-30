// qiven3_case_main: the main of every case binary, the log's. It holds the
// process's log (support/log/journal.hpp), which the runner hands to the
// cases, writes each case's case.end into it, and names a PlatformError a
// case lets out; the runner (runner/core.hpp) knows none of them.

#include <chrono>
#include <cstddef>
#include <cstdio>
#include <iostream>
#include <memory>
#include <string_view>
#include <vector>

#include "log/catalogue.hpp"
#include "log/log.hpp"
#include "platform/platform.hpp"
#include "runner/runner.hpp"
#include "support/log/journal.hpp"

namespace qiven3::test {

namespace {

// The outcomes' names, made once: a Name made within a conditional
// expression is not made right on every compiler.
constexpr log::Name passed = "pass";
constexpr log::Name skipped = "skip";
constexpr log::Name failed = "fail";

// A PlatformError, a plain value with no base: its code's name and its
// detail.
bool platform_named(std::ostream& out) {
    try {
        throw;
    } catch (const platform::PlatformError& e) {
        out << "platform error " << platform::name(e.code) << ": " << e.detail.view() << "\n";
        return true;
    } catch (...) {
        return false;
    }
}

const NamerRegistrar platform_namer{&platform_named};

}  // namespace

void case_ended(log::Log& log, const Case& c, int status, std::chrono::steady_clock::time_point start,
                std::size_t held) {
    const log::Name outcome = status == exit_pass ? passed : status == exit_skip ? skipped : failed;
    log::event(log, log::events::case_end, *c.logged, outcome, log::Millis::since(start), log::Count(held));
}

}  // namespace qiven3::test

int main(int argc, char** argv) {
    // A crash, an abort or a failed assertion is the diagnostic entry's,
    // which every binary runs before main: a report on standard error, the
    // process's status, and no dialog.
    // Unbuffered, so what a case wrote before a crash is in the log.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    // The process's log, its ring in a crash's report, drained at its end;
    // its pages on the heap, outliving it, so that no frame holds them.
    const auto pages = std::make_unique<qiven3::test::Journal::Pages>();
    qiven3::test::Journal journal(*pages, "runner");
    std::vector<std::string_view> args;
    for (int i = 1; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }
    return qiven3::test::run_main(args, qiven3::test::registry(), std::cout, std::cerr, journal.log(),
                                  &qiven3::test::case_ended);
}
