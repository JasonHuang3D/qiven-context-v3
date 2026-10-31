# The per-call wire: the request read within its bounds, each at its limit
# and one past, an empty vector, an unreadable one, arguments not strict
# UTF-8; the answer written whole on the fake, failed where its write
# throws and, under QIVEN_FAULTS, its platform call failed in turn; the
# heap counted by the binary's own operator new: nothing allocated.
qiven3_add_cases(qiven3_unit_wire LABELS fast fake
  SOURCES wire_test.cpp wire_memory_test.cpp
  LINK qiven3_wire qiven3_platform_fake)
