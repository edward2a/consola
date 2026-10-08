# SPDX-License-Identifier: MIT

set(CONSOLA_LIBTSM_SOURCES
    ${libtsm_SOURCE_DIR}/src/tsm/tsm-render.c
    ${libtsm_SOURCE_DIR}/src/tsm/tsm-screen.c
    ${libtsm_SOURCE_DIR}/src/tsm/tsm-selection.c
    ${libtsm_SOURCE_DIR}/src/tsm/tsm-unicode.c
    ${libtsm_SOURCE_DIR}/src/tsm/tsm-vte-charsets.c
    ${libtsm_SOURCE_DIR}/src/tsm/tsm-vte.c
    ${libtsm_SOURCE_DIR}/src/shared/shl-htable.c
    ${libtsm_SOURCE_DIR}/external/wcwidth/wcwidth.c
)

file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/generated)
configure_file(
    ${CMAKE_CURRENT_LIST_DIR}/libtsm-config.h.in
    ${CMAKE_BINARY_DIR}/generated/libtsm-config.h
    @ONLY
)

add_library(consola_libtsm STATIC ${CONSOLA_LIBTSM_SOURCES})
target_include_directories(consola_libtsm SYSTEM PUBLIC
    ${libtsm_SOURCE_DIR}/src/tsm
    ${libtsm_SOURCE_DIR}/src/shared
    ${libtsm_SOURCE_DIR}/external
    ${libtsm_SOURCE_DIR}/external/wcwidth
)
target_compile_options(consola_libtsm PRIVATE
    -std=gnu99
    -ffast-math
    -fno-strict-aliasing
    -ffunction-sections
    -fdata-sections
    -D_GNU_SOURCE
    -D_POSIX_C_SOURCE=200809L
    -include
    ${CMAKE_BINARY_DIR}/generated/libtsm-config.h
)
target_link_libraries(consola_libtsm PRIVATE consola_sanitizers)
set_target_properties(consola_libtsm PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS ON
)
