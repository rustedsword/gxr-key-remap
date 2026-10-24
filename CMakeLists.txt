cmake_minimum_required(VERSION 3.16)
project(gxr-key-remap VERSION 1.0.0 LANGUAGES C)

include(GNUInstallDirs)

option(GXR_INSTALL_SYSTEMD_UNITS "Install systemd service units" ON)
option(GXR_BUILD_TOOLS "Build the keys-remap-verify debugging tool" ON)

set(SYSTEMD_UNITDIR "${CMAKE_INSTALL_PREFIX}/lib/systemd/system"
    CACHE PATH "Directory for systemd service units")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
add_compile_options(-Wall -Wextra)

add_executable(keys-remap src/keys-remap.c)
add_executable(touchkey-remap src/touchkey-remap.c)
install(TARGETS keys-remap touchkey-remap
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

if(GXR_BUILD_TOOLS)
    # Development aid only; not installed.
    add_executable(keys-remap-verify tools/keys-remap-verify.c)
endif()

if(GXR_INSTALL_SYSTEMD_UNITS)
    foreach(unit keys-remap.service touchkey-remap.service)
        configure_file(systemd/${unit}.in ${CMAKE_CURRENT_BINARY_DIR}/${unit} @ONLY)
        install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${unit}
                DESTINATION ${SYSTEMD_UNITDIR})
    endforeach()
endif()

include(CTest)
if(BUILD_TESTING)
    add_test(NAME touchkey-remap-self-test
             COMMAND touchkey-remap --self-test)
endif()
