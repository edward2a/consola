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
option(CONSOLA_BUILD_BENCHMARKS "Build the Stage 7 benchmark harness" ON)

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
    src/app/tab.cpp
    src/app/tab_manager.cpp
    src/app/terminal_instance.cpp
    src/config/apply.cpp
    src/config/config.cpp
    src/config/loader.cpp
    src/config/restore.cpp
    src/config/writer.cpp
    src/layout/layout_geometry.cpp
    src/layout/layout_tree.cpp
    src/terminal/emulator.cpp
)
target_include_directories(terminal_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(terminal_core PUBLIC consola_warnings consola_sanitizers consola_tsan)

add_library(terminal_platform STATIC
    src/platform/linux/io_loop.cpp
    src/platform/linux/process.cpp
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

add_library(terminal_session STATIC
    src/app/terminal_manager.cpp
    src/config/capture.cpp
)
target_include_directories(terminal_session PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(terminal_session PUBLIC terminal_core terminal_platform)

# Embed a binary file as a C++ byte array (generated at configure time).
function(consola_embed_binary name input)
    if(NOT EXISTS "${input}")
        message(FATAL_ERROR "Embedded resource not found: ${input}")
    endif()
    set(header "${CMAKE_CURRENT_BINARY_DIR}/generated/${name}.hpp")
    file(READ "${input}" hex HEX)
    string(LENGTH "${hex}" hex_len)
    math(EXPR byte_count "${hex_len} / 2")
    string(REGEX REPLACE "(..)" "0x\\1," bytes "${hex}")
    file(WRITE "${header}"
        "#pragma once\n"
        "#include <array>\n"
        "#include <cstddef>\n"
        "namespace consola {\n"
        "inline constexpr std::array<unsigned char, ${byte_count}> ${name} = {${bytes}};\n"
        "} // namespace consola\n")
    set(${name}_HEADER "${header}" PARENT_SCOPE)
endfunction()

# Embedded fallback font: the terminal prefers the configured family (the
# bundled Inconsolata Condensed) but always has a usable monospace family even
# when the system lacks one or cannot render the preferred font.
consola_embed_binary(consola_dejavu_sans_mono
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts/DejaVuSansMono.ttf")

add_library(terminal_ui STATIC
    src/ui/application.cpp
    src/ui/commands.cpp
    src/ui/font_name.cpp
    src/ui/key_translator.cpp
    src/ui/shortcuts.cpp
    src/ui/tab_bar.cpp
    src/ui/terminal_frame.cpp
    src/ui/terminal_view.cpp
    src/ui/terminal_widget.cpp
    src/ui/url_detector.cpp
)
target_include_directories(terminal_ui PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
)
target_link_libraries(terminal_ui PUBLIC terminal_session)

include(cmake/deps.cmake)
include(cmake/licenses.cmake)

find_package(Fontconfig REQUIRED)

target_link_libraries(terminal_core PRIVATE consola_libtsm consola_ryml)
target_link_libraries(terminal_ui PUBLIC consola_fltk terminal_platform)
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

if(CONSOLA_BUILD_BENCHMARKS)
    add_subdirectory(bench)
endif()
