# The platform's own tests, each case binary as its sources need it. Added
# by tests/CMakeLists.txt, where qiven3_add_cases, qiven_faults and
# qiven_case_main are made. A case that reads a private header of the
# Windows adapter (windows/facts.hpp) reaches the module's src/ by its own
# include directory.
set(qiven_platform_src "${CMAKE_CURRENT_SOURCE_DIR}/../src")

# files' cases on the fake: its pure checks evaluated by the compiler;
# replace, stage and lock on real files in each case's scratch directory,
# the fake's busy counts driving the retries (P9, P19, P20's lock, P25's
# replace and open); stage's bound at its edge, each end freeing a slot;
# list, remove and rename (files_names_test.cpp), list's names counted on
# their arena; each system and platform call failed in turn.
qiven3_add_cases(qiven_platform_files_test LABELS fast fake
  SOURCES files_test.cpp files_names_test.cpp
  LINK qiven_faults qiven_platform_fake)

# The fake's processes held to their contract: start and run refusing a
# relative argv[0], a .cmd and a .bat before any lookup; read_line keeping
# its line on a failed allocation, by the budget and, under QIVEN_FAULTS,
# each of the line's allocations failed in turn.
qiven3_add_cases(qiven_platform_fake_process_test LABELS fast fake
  SOURCES fake_process_test.cpp
  LINK qiven_platform_fake)

# windows/files' cases on the Windows adapter, on Windows alone: create,
# read, remove_directory, replace and lock on real files in each case's
# scratch directory under the job's temporary directory, and list, remove
# and rename with each system error they meet measured (win_names_test.cpp);
# read's allocation counted; each Win32 call failed in turn. fast and fake:
# they need no processes, machine fact or debugger, so every Windows
# build's fake selection and CI's run them, as the fast one does.
if(WIN32)
  qiven3_add_cases(qiven_platform_win_files_test LABELS fast fake
    SOURCES win_files_test.cpp win_names_test.cpp
    LINK qiven_faults qiven_platform_windows)
endif()

# windows/files' stage on the Windows adapter, on Windows alone, as
# win_files': stage, open, write, close, publish and discard on real files;
# the bound of 16 open files to its edge; each Win32 call failed in turn;
# the heap counted by the binary's own operator new, which no other binary
# has.
if(WIN32)
  qiven3_add_cases(qiven_platform_win_files_stage_test LABELS fast fake
    SOURCES win_files_stage_test.cpp win_files_stage_memory_test.cpp
    LINK qiven_faults qiven_platform_windows)
endif()

# windows/arguments' run_utf8, wmain's body, on Windows alone: the
# program's name not read; what it cannot read or past a bound refused;
# under QIVEN_FAULTS the pages' call and each allocation failed in turn, a
# fault's status apart. fast and fake, as win_files'.
if(WIN32)
  qiven3_add_cases(qiven_platform_win_arguments_test LABELS fast fake
    SOURCES win_arguments_test.cpp
    LINK qiven_platform_windows)
endif()

# windows/facts' cases on the Windows adapter, on Windows alone, that read
# no machine fact: the rules the compiler evaluates, the names, each
# member's refusals and first failed call. fast and fake, as win_files'.
if(WIN32)
  qiven3_add_cases(qiven_platform_win_facts_test LABELS fast fake
    SOURCES win_facts_test.cpp
    LINK qiven_faults qiven_platform_windows)
  target_include_directories(qiven_platform_win_facts_test PRIVATE "${qiven_platform_src}")
endif()

# windows/facts' cases on this machine's facts: facts, residual and synced
# read here against the case's own reads, their allocations counted, each
# platform call failed in turn. machine: they read the machine's facts, so
# the full and asan presets run them, not the fast or fake ones.
if(WIN32)
  qiven3_add_cases(qiven_platform_win_facts_machine_test LABELS machine
    SOURCES win_facts_machine_test.cpp win_facts_faults_test.cpp
    LINK qiven_faults qiven_platform_windows)
  target_include_directories(qiven_platform_win_facts_machine_test PRIVATE "${qiven_platform_src}")
endif()
