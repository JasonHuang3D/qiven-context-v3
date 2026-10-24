// The assertion's plant, without NDEBUG in every build, so that
// assert(false) is compiled in. On the Windows debug runtime the plant is
// _ASSERTE(false) first, the runtime's own assertion, which its report
// hook takes: the C runtime's assert writes its own line and aborts,
// reaching no hook.

#undef NDEBUG
#include <cassert>

#ifdef _DEBUG
#include <crtdbg.h>
#endif

#ifdef _MSC_VER
__declspec(noinline)
#else
__attribute__((noinline))
#endif
int plant_assert() {
#ifdef _DEBUG
    _ASSERTE(false);
#endif
    assert(false);
    return 1;
}
