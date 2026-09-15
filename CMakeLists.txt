cmake_minimum_required(VERSION 3.14)
project(nlos_doppler_fuzzer CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_path(Z3_INCLUDE_DIR NAMES z3++.h PATHS /usr/include /usr/local/include)
find_library(Z3_LIBRARY NAMES z3 PATHS /usr/lib /usr/lib/x86_64-linux-gnu /usr/local/lib)

if(NOT Z3_INCLUDE_DIR OR NOT Z3_LIBRARY)
    message(FATAL_ERROR "Z3 development files not found.")
endif()

include_directories(${Z3_INCLUDE_DIR} src)

add_executable(advanced_engine src/advanced_engine.cpp src/sancov_hook.cpp)

# Target options without coverage for all files
target_compile_options(advanced_engine PRIVATE -O2 -g -fsanitize=address,undefined -fno-omit-frame-pointer)

# Apply coverage instrumentation ONLY to advanced_engine.cpp, leaving sancov_hook.cpp untouched
set_source_files_properties(src/advanced_engine.cpp PROPERTIES COMPILE_FLAGS "-fsanitize-coverage=trace-pc")

target_link_options(advanced_engine PRIVATE -fsanitize=address,undefined -fsanitize-coverage=trace-pc)
target_link_libraries(advanced_engine PRIVATE ${Z3_LIBRARY} pthread rt dl)

add_executable(replay_engine src/replay_engine.cpp)
target_link_libraries(replay_engine PRIVATE pthread)
