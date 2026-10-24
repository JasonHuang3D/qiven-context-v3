# qiven3: the product binary, its subcommands, `qiven3 location` first,
# with its instantiation file for the build's one adapter. main and the
# instantiation file compile into the binary alone, outside core's targets;
# it links current and adapter_for, which a main alone links.
qiven3_executable(qiven3 SOURCES main.cpp main.hpp main_impl.hpp instances.cpp
  LINK qiven3_location qiven3_platform_current)
target_include_directories(qiven3 PRIVATE "${PROJECT_SOURCE_DIR}/src")
