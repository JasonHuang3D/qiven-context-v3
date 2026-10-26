// win_process_case's unit for the analysis preset: its helpers over the
// Windows adapter, on Windows alone, thrown instantiated for a plain
// function, as the cases bind it to their lambdas.

#if defined(_WIN32)

#include "support/fixtures/win_process_case.hpp"

namespace qiven3::test::process {

template Thrown thrown<void (*)()>(void (*&&)());

}  // namespace qiven3::test::process

#endif
