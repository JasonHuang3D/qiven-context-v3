// qiven3_fault_lib: the test tree's own shared library. qiven3-fault-import
// imports it, for K6, the library missing at load, and K11's control, an
// import outside the system's list.

#ifdef _WIN32
#define QIVEN3_FAULT_LIB extern "C" __declspec(dllexport)
#else
#define QIVEN3_FAULT_LIB extern "C" __attribute__((visibility("default")))
#endif

QIVEN3_FAULT_LIB int qiven3_fault_lib_value() { return 0; }
