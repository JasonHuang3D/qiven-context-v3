// qiven3-fault-import: imports qiven3_fault_lib and exits with its value, 0.
// K6 runs it copied alone into a directory, its library missing at load;
// K11 finds the import outside the system's list. On Windows the call goes
// through the import library's stub, not __imp_, so that B5 reads no import
// from a Windows library.

extern "C" int qiven3_fault_lib_value();

int main() { return qiven3_fault_lib_value(); }
