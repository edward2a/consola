# SPDX-License-Identifier: MIT

add_executable(consola_link_check link_check.cpp)
target_link_libraries(consola_link_check PRIVATE
    terminal_ui terminal_core consola_libtsm consola_ryml consola_fltk)

add_test(NAME link_check COMMAND consola_link_check)

add_executable(consola_log_test log_test.cpp)
target_link_libraries(consola_log_test PRIVATE terminal_core)

add_test(NAME log_test COMMAND consola_log_test)

add_executable(consola_emulator_test emulator_test.cpp)
target_link_libraries(consola_emulator_test PRIVATE terminal_core)

add_test(NAME emulator_test COMMAND consola_emulator_test)

add_test(NAME smoke_test
    COMMAND ${CMAKE_SOURCE_DIR}/helper-scripts/run-headless.sh
            $<TARGET_FILE:terminal_app> --smoke-test)
set_tests_properties(smoke_test PROPERTIES TIMEOUT 30)

if(CONSOLA_SANITIZERS)
    set_tests_properties(smoke_test PROPERTIES
        ENVIRONMENT "LSAN_OPTIONS=suppressions=${CMAKE_SOURCE_DIR}/tests/lsan.supp")
endif()
