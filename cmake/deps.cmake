# SPDX-License-Identifier: MIT

include(FetchContent)

# Pinned dependency revisions; see docs/dependencies.md.
set(CONSOLA_FLTK_VERSION 1.4.5)
set(CONSOLA_FLTK_GIT_TAG a9b1113516ffd15fc7602a6d425a317df30f4720) # release-1.4.5
set(CONSOLA_LIBTSM_VERSION 4.8.0)
set(CONSOLA_LIBTSM_GIT_TAG ef2365d0f9f1a370d721a82470d69c3646fe97f0) # v4.8.0
set(CONSOLA_RYML_VERSION 0.16.0)
set(CONSOLA_RYML_GIT_TAG f8ac8dd50f4f7916579d55a05ebf9c6488e52670) # v0.16.0

# System FLTK must be X11/Xft-only; the package exports no backend metadata, so
# this probe uses its link dependencies and fails closed when it cannot verify.
function(consola_require_fltk_x11_only target)
    get_target_property(_consola_fltk_libs ${target} INTERFACE_LINK_LIBRARIES)
    if(NOT _consola_fltk_libs)
        message(FATAL_ERROR
            "Cannot verify ${target} is X11/Xft-only: no link metadata. "
            "System FLTK is unsupported; build FLTK from source with CONSOLA_USE_SYSTEM_DEPS=OFF.")
    endif()
    string(TOLOWER "${_consola_fltk_libs}" _consola_fltk_libs_lc)
    if(_consola_fltk_libs_lc MATCHES "wayland|cairo|pango")
        message(FATAL_ERROR "System FLTK is not X11-only (links Wayland/Cairo/Pango): ${_consola_fltk_libs}")
    endif()
    if(NOT _consola_fltk_libs_lc MATCHES "x11")
        message(FATAL_ERROR "Cannot verify system FLTK uses X11: ${_consola_fltk_libs}")
    endif()
    if(NOT _consola_fltk_libs_lc MATCHES "xft")
        message(FATAL_ERROR "Cannot verify system FLTK uses Xft: ${_consola_fltk_libs}")
    endif()
endfunction()

# System rapidyaml must use the exception-enabled default error callbacks.
function(consola_require_ryml_exceptions target)
    get_target_property(_consola_ryml_defs ${target} INTERFACE_COMPILE_DEFINITIONS)
    if(NOT _consola_ryml_defs)
        message(FATAL_ERROR
            "Cannot verify ${target} enables exception error callbacks: no exported compile definitions. "
            "System rapidyaml is unsupported; build rapidyaml from source with CONSOLA_USE_SYSTEM_DEPS=OFF.")
    endif()
    if(NOT _consola_ryml_defs MATCHES "RYML_DEFAULT_CALLBACK_USES_EXCEPTIONS")
        message(FATAL_ERROR "System rapidyaml does not enable exception callbacks: ${_consola_ryml_defs}")
    endif()
endfunction()

if(CONSOLA_USE_SYSTEM_DEPS)
    find_package(FLTK ${CONSOLA_FLTK_VERSION} CONFIG REQUIRED)
    find_package(ryml ${CONSOLA_RYML_VERSION} CONFIG REQUIRED)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBTSM REQUIRED IMPORTED_TARGET libtsm>=${CONSOLA_LIBTSM_VERSION})

    consola_require_fltk_x11_only(fltk::fltk)
    consola_require_ryml_exceptions(ryml::ryml)

    add_library(consola_fltk INTERFACE)
    target_link_libraries(consola_fltk INTERFACE fltk::fltk)

    add_library(consola_ryml INTERFACE)
    target_link_libraries(consola_ryml INTERFACE ryml::ryml)

    add_library(consola_libtsm INTERFACE)
    target_link_libraries(consola_libtsm INTERFACE PkgConfig::LIBTSM)
else()
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

    set(FLTK_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(FLTK_BUILD_TEST OFF CACHE BOOL "" FORCE)
    set(FLTK_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(FLTK_BUILD_FLUID OFF CACHE BOOL "" FORCE)
    set(FLTK_BUILD_FLTK_OPTIONS OFF CACHE BOOL "" FORCE)
    set(FLTK_BUILD_FORMS OFF CACHE BOOL "" FORCE)
    set(FLTK_BUILD_GL OFF CACHE BOOL "" FORCE)
    set(FLTK_BUILD_HTML_DOCS OFF CACHE BOOL "" FORCE)
    set(FLTK_BUILD_PDF_DOCS OFF CACHE BOOL "" FORCE)
    set(FLTK_BUILD_FLUID_DOCS OFF CACHE BOOL "" FORCE)
    set(FLTK_USE_PTHREADS ON CACHE BOOL "" FORCE)
    set(FLTK_BACKEND_X11 ON CACHE BOOL "" FORCE)
    set(FLTK_BACKEND_WAYLAND OFF CACHE BOOL "" FORCE)
    set(FLTK_USE_XFT ON CACHE BOOL "" FORCE)
    set(FLTK_USE_PANGO OFF CACHE BOOL "" FORCE)
    set(FLTK_GRAPHICS_CAIRO OFF CACHE BOOL "" FORCE)
    set(FLTK_OPTION_CAIRO_WINDOW OFF CACHE BOOL "" FORCE)
    set(FLTK_OPTION_CAIRO_EXT OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(fltk
        GIT_REPOSITORY https://github.com/fltk/fltk.git
        GIT_TAG ${CONSOLA_FLTK_GIT_TAG}
        EXCLUDE_FROM_ALL
    )
    FetchContent_MakeAvailable(fltk)

    add_library(consola_fltk INTERFACE)
    target_link_libraries(consola_fltk INTERFACE fltk::fltk)

    set(RYML_STANDALONE ON CACHE BOOL "" FORCE)
    set(RYML_SYSTEM_C4CORE OFF CACHE BOOL "" FORCE)
    set(C4_LIBRARY_TYPE STATIC CACHE STRING "" FORCE)
    set(RYML_CXX_STANDARD 20 CACHE STRING "" FORCE)
    set(RYML_DEV OFF CACHE BOOL "" FORCE)
    set(RYML_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(RYML_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
    set(RYML_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
    set(RYML_BUILD_API OFF CACHE BOOL "" FORCE)
    set(RYML_FUZZ_DRIVERS OFF CACHE BOOL "" FORCE)
    set(RYML_EXTRA_ALL OFF CACHE BOOL "" FORCE)
    set(RYML_EXTRA_INTS OFF CACHE BOOL "" FORCE)
    set(RYML_EXTRA_INTS_UTILS OFF CACHE BOOL "" FORCE)
    set(RYML_EXTRA_INTS_TESTSUITE OFF CACHE BOOL "" FORCE)
    set(RYML_DEFAULT_CALLBACKS ON CACHE BOOL "" FORCE)
    set(RYML_DEFAULT_CALLBACK_USES_EXCEPTIONS ON CACHE BOOL "" FORCE)

    set(RYML_INSTALL OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(ryml
        GIT_REPOSITORY https://github.com/biojppm/rapidyaml.git
        GIT_TAG ${CONSOLA_RYML_GIT_TAG}
        EXCLUDE_FROM_ALL
    )
    FetchContent_MakeAvailable(ryml)

    add_library(consola_ryml INTERFACE)
    target_link_libraries(consola_ryml INTERFACE ryml::ryml)

    FetchContent_Declare(libtsm
        GIT_REPOSITORY https://github.com/kmscon/libtsm.git
        GIT_TAG ${CONSOLA_LIBTSM_GIT_TAG}
    )
    FetchContent_MakeAvailable(libtsm)

    include(${CMAKE_CURRENT_LIST_DIR}/libtsm.cmake)
endif()
