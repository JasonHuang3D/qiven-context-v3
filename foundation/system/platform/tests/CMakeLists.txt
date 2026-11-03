# The platform's own tests, each case binary as its sources need it. Added
# by tests/CMakeLists.txt, where qiven3_add_cases, qiven_faults and
# qiven_case_main are made. A case that reads a private header of the
# Windows adapter (windows/facts.hpp) reaches the module's src/ by its own
# include directory, written without .. (structure's B11).
cmake_path(GET CMAKE_CURRENT_SOURCE_DIR PARENT_PATH qiven_platform_dir)
set(qiven_platform_src "${qiven_platform_dir}/src")

# files' cases on the fake: its pure checks evaluated by the compiler;
# replace, stage and lock on real files in each case's scratch directory,
# the fake's busy counts driving the retries, a deadline of 0 one try (P9,
# P19, P20's lock, P25's replace and open); replace's calls failed in
# turn, the old bytes whole
# after each (P10's); stage's bound at its edge, each end freeing a slot;
# list, remove and rename (files_names_test.cpp), list's names counted on
# their arena, and the fake's members over them, a file renamed to its own
# name kept and each refused by its fault point (P27's names); each system
# and platform call failed in turn.
qiven3_add_cases(qiven_platform_files_test LABELS fast fake
  SOURCES files_test.cpp files_names_test.cpp
  LINK qiven_faults qiven_platform_fake)

# The fake's processes held to their contract: start and run refusing a
# relative argv[0], a .cmd and a .bat before any lookup; read_line keeping
# its line on a failed allocation, by the budget and, under QIVEN_FAULTS,
# each of the line's allocations failed in turn; a program's working
# directory, the caller's or the one given; its programs killed, hanging,
# flooding, exiting with a code at each cap, found by their whole argv
# alone (P11's to P13's and P15's on the fake). And its own members
# beyond files (fake_platform_test.cpp): config_dir's location made only by
# create, and its errors (P2's); normcase folding ASCII letters alone
# (P29's); wipe zeroing, each call recorded (P30's); random_bytes by its
# seed's blocks (P23's); now as its clock in milliseconds (P24's); pages
# zeroed, refused when set to, lent counting them (P28's); resolve and a
# Staged's open refusing a path not UTF-8 (P25's); create, read and
# remove_directory on real files, two fakes' creates racing (P27's).
qiven3_add_cases(qiven_platform_fake_process_test LABELS fast fake
  SOURCES fake_process_test.cpp fake_platform_test.cpp
  LINK qiven_platform_fake)

# current's choice of the build's adapter, on every system: a system with
# no adapter built refused, naming it (P1's); the build's own chosen.
qiven3_add_cases(qiven_platform_current_test LABELS fast fake
  SOURCES current_test.cpp
  LINK qiven_platform_current)

# windows/files' cases on the Windows adapter, on Windows alone: create,
# read, remove_directory, replace and lock on real files in each case's
# scratch directory under the job's temporary directory, two adapters'
# creates racing (P27's), replace held by a reader on the target or on its
# temporary file and lock by another holder, each released before the
# deadline or held past it (P9's and P20's lock half; the race and the
# holders in win_files_held_test.cpp), and list, remove and rename with
# each system error they meet measured (win_names_test.cpp); read's
# allocation counted; each Win32 call failed in turn. fast and fake: they
# need no processes, machine fact or debugger, so every Windows build's
# fake selection and CI's run them, as the fast one does.
if(WIN32)
  qiven3_add_cases(qiven_platform_win_files_test LABELS fast fake
    SOURCES win_files_test.cpp win_files_held_test.cpp win_names_test.cpp
    LINK qiven_faults qiven_platform_windows)
endif()

# windows/files' stage on the Windows adapter, on Windows alone, as
# win_files': stage, open, write, close, publish and discard on real files;
# the bound, the case's own 4 open files, to its edge; each Win32 call
# failed in turn; the heap counted by the binary's own operator new, which
# no other binary has.
if(WIN32)
  qiven3_add_cases(qiven_platform_win_files_stage_test LABELS fast fake
    SOURCES win_files_stage_test.cpp win_files_stage_memory_test.cpp
    LINK qiven_faults qiven_platform_windows)
endif()

# windows/arguments' run_utf8, wmain's body, on Windows alone: the
# program's name not read; what it cannot read or past a bound refused;
# under QIVEN_FAULTS the pages' call and each allocation failed in turn, a
# fault's status apart. And the adapter's own members in this process
# (win_platform_test.cpp): config_dir's override and its default location,
# which LOCALAPPDATA does not move (P2's); normcase's forms (P29's); wipe's
# zeros (P30's); random_bytes' draws differing (P23's); now beside the
# system's clock (P24's); pages zeroed, a count the system refuses memory
# (P28's); resolve, replace and a Staged's open refusing a path not UTF-8
# (P25's). fast and fake, as win_files'.
if(WIN32)
  qiven3_add_cases(qiven_platform_win_arguments_test LABELS fast fake
    SOURCES win_arguments_test.cpp win_platform_test.cpp
    LINK qiven_platform_windows)
endif()

# qiven_platform_child, on Windows alone: the children of the process
# cases, win_process' below and a caller's over the contract, each a form
# of its table
# (child/child.cpp), its entry wmain by the adapter's run_utf8 (WIDE). It
# links the adapter alone, and diag, last, brings memory.
if(WIN32)
  qiven_executable(qiven_platform_child SOURCES child/child.cpp LINK qiven_platform_windows WIDE)
  target_compile_definitions(qiven_platform_child PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
endif()

# windows/process' cases on the Windows adapter, on Windows alone: program;
# start's refusals; each argument, the environment, the input, wait, kill
# and the tree's descendants through qiven_platform_child; read_line, the
# caps, the deadline and run; standard_output to a pipe, the child's, in
# the code page's bytes (P26's); the working directory; replace whole in
# a child killed between its write and its rename (P10's); the heap
# counted by the binary's own operator new, the jthreads' blocks included;
# each Win32 call and arena allocation failed in turn. Their helpers are
# win_process_case.hpp. machine: they start processes, so the full and
# asan presets run them, not the fast or fake ones.
if(WIN32)
  qiven3_add_cases(qiven_platform_win_process_test LABELS machine
    SOURCES win_process_test.cpp win_process_streams_test.cpp win_process_memory_test.cpp
            win_process_faults_test.cpp
    LINK qiven_faults qiven_platform_windows)
  add_dependencies(qiven_platform_win_process_test qiven_platform_child)
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
