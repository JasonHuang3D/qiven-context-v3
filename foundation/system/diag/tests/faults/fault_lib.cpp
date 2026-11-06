// qiven_diag_fault_lib: the test tree's own shared library.
// qiven_diag_fault_import imports it, for K6, the library missing at load,
// and K11's control, an import outside the system's list.

#ifdef _WIN32
#define QIVEN_FAULT_LIB extern "C" __declspec(dllexport)
#else
#define QIVEN_FAULT_LIB extern "C" __attribute__((visibility("default")))
#endif

QIVEN_FAULT_LIB int qiven_diag_fault_lib_value() { return 0; }
