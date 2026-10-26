# SPDX-License-Identifier: MIT

add_executable(consola_bench bench_main.cpp)
target_link_libraries(consola_bench PRIVATE terminal_ui terminal_session)
set_target_properties(consola_bench PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
