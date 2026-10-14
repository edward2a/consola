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

add_executable(consola_layout_test layout_test.cpp)
target_link_libraries(consola_layout_test PRIVATE terminal_core)

add_test(NAME layout_test COMMAND consola_layout_test)

add_executable(consola_layout_geometry_test layout_geometry_test.cpp)
target_link_libraries(consola_layout_geometry_test PRIVATE terminal_core)

add_test(NAME layout_geometry_test COMMAND consola_layout_geometry_test)

add_executable(consola_tab_test tab_test.cpp)
target_link_libraries(consola_tab_test PRIVATE terminal_core)

add_test(NAME tab_test COMMAND consola_tab_test)

add_executable(consola_terminal_manager_test terminal_manager_test.cpp)
target_link_libraries(consola_terminal_manager_test PRIVATE terminal_session)

add_test(NAME terminal_manager_test COMMAND consola_terminal_manager_test)
set_tests_properties(terminal_manager_test PROPERTIES TIMEOUT 60)

add_executable(consola_key_translator_test key_translator_test.cpp)
target_link_libraries(consola_key_translator_test PRIVATE terminal_ui)

add_test(NAME key_translator_test COMMAND consola_key_translator_test)

add_executable(consola_pty_session_test pty_session_test.cpp)
target_link_libraries(consola_pty_session_test PRIVATE terminal_platform)

add_test(NAME pty_session_test COMMAND consola_pty_session_test)
set_tests_properties(pty_session_test PROPERTIES TIMEOUT 60)

add_executable(consola_wakeup_test wakeup_test.cpp)
target_link_libraries(consola_wakeup_test PRIVATE terminal_core)

add_test(NAME wakeup_test COMMAND consola_wakeup_test)

add_executable(consola_session_queue_test session_queue_test.cpp)
target_link_libraries(consola_session_queue_test PRIVATE terminal_core)

add_test(NAME session_queue_test COMMAND consola_session_queue_test)

add_executable(consola_io_loop_test io_loop_test.cpp)
target_link_libraries(consola_io_loop_test PRIVATE terminal_platform)

add_test(NAME io_loop_test COMMAND consola_io_loop_test)
set_tests_properties(io_loop_test PROPERTIES TIMEOUT 60)

add_test(NAME smoke_test
    COMMAND ${CMAKE_SOURCE_DIR}/helper-scripts/run-headless.sh
            $<TARGET_FILE:terminal_app> --smoke-test)
set_tests_properties(smoke_test PROPERTIES TIMEOUT 30)

add_test(NAME session_smoke_test
    COMMAND ${CMAKE_SOURCE_DIR}/helper-scripts/run-headless.sh
            $<TARGET_FILE:terminal_app>)
set(session_smoke_env "SHELL=/bin/sh;CONSOLA_EXIT_AFTER_MS=500")

if(CONSOLA_SANITIZERS)
    set(lsan_options "LSAN_OPTIONS=suppressions=${CMAKE_SOURCE_DIR}/tests/lsan.supp")
    set_tests_properties(smoke_test PROPERTIES ENVIRONMENT "${lsan_options}")
    set(session_smoke_env "${session_smoke_env};${lsan_options}")
endif()

set_tests_properties(session_smoke_test PROPERTIES
    TIMEOUT 30
    ENVIRONMENT "${session_smoke_env}")
