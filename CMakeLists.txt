# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.28)

project(consola
    VERSION 0.1.0
    DESCRIPTION "Lightweight, configurable terminal emulator"
    LANGUAGES C CXX
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

option(CONSOLA_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(CONSOLA_TSAN "Enable ThreadSanitizer" OFF)
option(CONSOLA_WARNINGS_AS_ERRORS "Treat project warnings as errors" OFF)
option(CONSOLA_USE_SYSTEM_DEPS "Use system-installed dependencies instead of FetchContent" OFF)

if(CONSOLA_TSAN AND CONSOLA_SANITIZERS)
    message(FATAL_ERROR "CONSOLA_TSAN and CONSOLA_SANITIZERS are mutually exclusive")
endif()

include(CTest)

add_library(consola_warnings INTERFACE)
target_compile_options(consola_warnings INTERFACE
    $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-Wall;-Wextra;-Wpedantic;-Wshadow;-Wnon-virtual-dtor;-Wcast-align;-Wunused;-Woverloaded-virtual;-Wnull-dereference;-Wdouble-promotion>
    $<$<COMPILE_LANG_AND_ID:C,GNU,Clang,AppleClang>:-Wall;-Wextra;-Wpedantic>
    $<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/W4;/permissive->
    $<$<COMPILE_LANG_AND_ID:C,MSVC>:/W4>
)

if(CONSOLA_WARNINGS_AS_ERRORS)
    target_compile_options(consola_warnings INTERFACE
        $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-Werror>
        $<$<COMPILE_LANG_AND_ID:C,GNU,Clang,AppleClang>:-Werror>
        $<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/WX>
        $<$<COMPILE_LANG_AND_ID:C,MSVC>:/WX>
    )
endif()

add_library(consola_sanitizers INTERFACE)
if(CONSOLA_SANITIZERS)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        message(FATAL_ERROR "CONSOLA_SANITIZERS is only supported with GNU, Clang, or AppleClang")
    endif()
    target_compile_options(consola_sanitizers INTERFACE
        $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-fsanitize=address,undefined;-fno-omit-frame-pointer>
        $<$<COMPILE_LANG_AND_ID:C,GNU,Clang,AppleClang>:-fsanitize=address,undefined;-fno-omit-frame-pointer>
    )
    target_link_options(consola_sanitizers INTERFACE
        $<$<LINK_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-fsanitize=address,undefined>
        $<$<LINK_LANG_AND_ID:C,GNU,Clang,AppleClang>:-fsanitize=address,undefined>
    )
endif()

add_library(consola_tsan INTERFACE)
if(CONSOLA_TSAN)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        message(FATAL_ERROR "CONSOLA_TSAN is only supported with GNU, Clang, or AppleClang")
    endif()
    target_compile_options(consola_tsan INTERFACE
        $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-fsanitize=thread;-fno-omit-frame-pointer>
        $<$<COMPILE_LANG_AND_ID:C,GNU,Clang,AppleClang>:-fsanitize=thread;-fno-omit-frame-pointer>
    )
    target_link_options(consola_tsan INTERFACE
        $<$<LINK_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-fsanitize=thread>
        $<$<LINK_LANG_AND_ID:C,GNU,Clang,AppleClang>:-fsanitize=thread>
    )
endif()

add_library(terminal_core STATIC
    src/app/log.cpp
    src/app/session_queue.cpp
    src/terminal/emulator.cpp
)
target_include_directories(terminal_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(terminal_core PUBLIC consola_warnings consola_sanitizers consola_tsan)

add_library(terminal_platform STATIC
    src/platform/linux/io_loop.cpp
    src/platform/linux/pty_session.cpp
    src/platform/linux/startup_worker.cpp
)
target_include_directories(terminal_platform PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_include_directories(terminal_platform PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_definitions(terminal_platform PRIVATE _GNU_SOURCE)
target_link_libraries(terminal_platform PUBLIC terminal_core)

include(CheckSymbolExists)
set(CMAKE_REQUIRED_DEFINITIONS -D_GNU_SOURCE)
check_symbol_exists(openpty "pty.h" CONSOLA_OPENPTY_IN_LIBC)
unset(CMAKE_REQUIRED_DEFINITIONS)
if(NOT CONSOLA_OPENPTY_IN_LIBC)
    find_library(CONSOLA_LIBUTIL util REQUIRED)
    target_link_libraries(terminal_platform PRIVATE ${CONSOLA_LIBUTIL})
endif()

add_library(terminal_ui STATIC
    src/ui/application.cpp
    src/ui/key_translator.cpp
    src/ui/terminal_widget.cpp
)
target_link_libraries(terminal_ui PUBLIC terminal_core)

include(cmake/deps.cmake)
include(cmake/licenses.cmake)

find_package(Fontconfig REQUIRED)

target_link_libraries(terminal_core PRIVATE consola_libtsm consola_ryml)
target_link_libraries(terminal_ui PUBLIC consola_fltk)
target_link_libraries(terminal_ui PRIVATE Fontconfig::Fontconfig)
target_compile_definitions(terminal_ui PRIVATE
    CONSOLA_FONT_FILE="${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts/Inconsolata-Condensed.ttf")

add_executable(terminal_app src/app/main.cpp)
target_link_libraries(terminal_app PRIVATE terminal_ui terminal_platform terminal_core)

install(TARGETS terminal_app RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(FILES resources/fonts/Inconsolata-Condensed.ttf
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/consola/fonts)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
