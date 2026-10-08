# SPDX-License-Identifier: MIT

include(GNUInstallDirs)

set(CONSOLA_LICENSE_DEST ${CMAKE_INSTALL_DATAROOTDIR}/doc/consola/licenses)

function(consola_install_license component)
    foreach(file ${ARGN})
        if(NOT EXISTS "${file}")
            message(FATAL_ERROR "Required license/notice file not found: ${file}")
        endif()
        install(FILES "${file}" DESTINATION "${CONSOLA_LICENSE_DEST}/${component}")
    endforeach()
endfunction()

# System-dependency mode does not stage dependency notices: those packages are
# installed and owned by the system package manager. Only consola's own license
# is installed in that mode.
if(NOT CONSOLA_USE_SYSTEM_DEPS)
    consola_install_license(fltk
        ${fltk_SOURCE_DIR}/COPYING)
    consola_install_license(libtsm
        ${libtsm_SOURCE_DIR}/COPYING
        ${libtsm_SOURCE_DIR}/LICENSE_htable
        ${libtsm_SOURCE_DIR}/external/wcwidth/LICENSE.txt
        ${libtsm_SOURCE_DIR}/external/xkbcommon/xkbcommon-keysyms.h)
    consola_install_license(rapidyaml
        ${ryml_SOURCE_DIR}/LICENSE.txt
        ${ryml_SOURCE_DIR}/ext/c4core.src/c4/ext/fast_float_all.h
        ${ryml_SOURCE_DIR}/ext/c4core.src/c4/ext/debugbreak/debugbreak.h)
endif()

install(FILES ${CMAKE_SOURCE_DIR}/LICENSE DESTINATION ${CONSOLA_LICENSE_DEST}/consola)

consola_install_license(inconsolata
    ${CMAKE_SOURCE_DIR}/resources/fonts/OFL.txt)
