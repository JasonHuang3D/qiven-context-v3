#pragma once

// signal: Linux's fault handlers and the report they write (signal.cpp),
// alone in their file so that K18 reads the object's undefined symbols
// against the async-signal-safe list. install() (diag_linux.cpp) sets the
// handler and the alternate stack; the terminate handler's report ends here
// too.
//
// thread: either from any thread; a report once, a second ending the
// process at once with the first's signal.

#include <signal.h>

#include <string_view>

namespace qiven3::diag::detail {

// The handler for SIGSEGV, SIGBUS, SIGFPE, SIGILL and SIGABRT, set with
// SA_SIGINFO | SA_ONSTACK | SA_RESETHAND | SA_NODEFER: the cause, the raw
// stack, and the end by the signal itself under the default action.
void on_signal(int number, siginfo_t* info, void* context) noexcept;

// The report with `cause` as its first line after "fatal: ", ended by
// signal `number` under the default action.
[[noreturn]] void report(std::string_view cause, int number) noexcept;

}  // namespace qiven3::diag::detail
