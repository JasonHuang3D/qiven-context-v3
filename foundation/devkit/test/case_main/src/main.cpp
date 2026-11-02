// qiven_case_main: the main of every case binary, the log's. It holds the
// process's log (qiven/case_main/journal.hpp), which the runner hands to the
// cases, writes each case's case.end into it, and names a PlatformError a
// case lets out; the runner (qiven/runner/core.hpp) knows none of them.

#include <chrono>
#include <cstddef>
#include <cstdio>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

#include "log/catalogue.hpp"
#include "log/log.hpp"
#include "memory/fail.hpp"
#include "platform/platform.hpp"
#include "qiven/case_main/journal.hpp"
#include "qiven/runner/runner.hpp"

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

}  // namespace

void case_ended(log::Log& log, const qiven::runner::Case& c, int status, std::chrono::steady_clock::time_point start,
                std::size_t held) {
    const log::Name outcome = status == qiven::runner::exit_pass ? passed : status == qiven::runner::exit_skip ? skipped : failed;
    log::event(log, log::events::case_end, *c.logged, outcome, log::Millis::since(start), log::Count(held));
}

}  // namespace qiven3::test

int main(int argc, char** argv) {
    // A crash, an abort or a failed assertion is the diagnostic entry's,
    // which every binary runs before main: a report on standard error, the
    // process's status, and no dialog.
    // Unbuffered, so what a case wrote before a crash is in the log.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    // An exception out of the run ends the process by the fail-fast.
    try {
        // The PlatformError's namer, after every module's static one.
        const qiven::runner::NamerRegistrar platform_namer{&qiven3::test::platform_named};
        // The process's log, its ring in a crash's report, drained at its end;
        // its pages on the heap, outliving it, so that no frame holds them.
        std::vector<qiven3::test::Journal::Pages> pages(1);
        qiven3::test::Journal journal(pages.front(), "runner");
        const std::span<char*> given(argv, static_cast<std::size_t>(argc));
        std::vector<std::string_view> args;
        for (std::size_t i = 1; i < given.size(); ++i) {
            args.emplace_back(given[i]);
        }
        return qiven::runner::run_main(args, qiven::runner::registry(), std::cout, std::cerr, journal.log(),
                                      &qiven3::test::case_ended);
    } catch (...) {
        qiven3::memory::fail_fast("case main: an exception out of the run");
    }
}
