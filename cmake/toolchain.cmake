# the programs of the build and the machine it builds for.

foreach(tool lld-link llvm-nm llvm-rc llvm-strip llvm-readobj llvm-dlltool)
    string(TOUPPER "${tool}" variable)
    string(REPLACE "-" "_" variable "MIME_${variable}")
    find_program(${variable} ${tool} REQUIRED)
endforeach()
find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
find_program(MIME_GLSLC glslc)

# x64 on linux x86_64, arm64 elsewhere, the processor the virtual machine runs.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$" AND NOT APPLE)
    set(default_guest x64)
else()
    set(default_guest arm64)
endif()
set(MIME_GUEST_ARCH ${default_guest} CACHE STRING "processor of the guest kernel and system dlls, x64 or arm64")
set_property(CACHE MIME_GUEST_ARCH PROPERTY STRINGS x64 arm64)
if(NOT MIME_GUEST_ARCH MATCHES "^(x64|arm64)$")
    message(FATAL_ERROR "MIME_GUEST_ARCH must be x64 or arm64")
endif()

# the host side needs a hypervisor backend for the guest: hypervisor.framework on arm64 macs, kvm on x86_64 linux.
if((APPLE AND MIME_GUEST_ARCH STREQUAL "arm64") OR (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND MIME_GUEST_ARCH STREQUAL "x64"
        AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64)$"))
    set(default_host ON)
else()
    set(default_host OFF)
endif()
option(MIME_BUILD_HOST "build the mime command, which embeds the guest" ${default_host})
if(NOT default_host AND MIME_BUILD_HOST)
    message(FATAL_ERROR "no hypervisor backend runs a ${MIME_GUEST_ARCH} guest on ${CMAKE_SYSTEM_NAME} ${CMAKE_SYSTEM_PROCESSOR}")
endif()
if(NOT MIME_BUILD_HOST)
    message(STATUS "mime: building the ${MIME_GUEST_ARCH} guest only, ${CMAKE_SYSTEM_NAME} ${CMAKE_SYSTEM_PROCESSOR} has no "
        "hypervisor backend for it")
endif()

# test programs that import what windows exports, not what mime does: mingw-w64's import libraries.
set(MIME_MINGW_LIB_x64 /usr/x86_64-w64-mingw32/lib CACHE PATH "mingw-w64 import libraries for x64")
set(MIME_MINGW_LIB_x86 /usr/i686-w64-mingw32/lib CACHE PATH "mingw-w64 import libraries for x86")

set(MIME_GEN ${CMAKE_BINARY_DIR}/gen)
set(MIME_SYSTEM32 ${CMAKE_BINARY_DIR}/guest/system32)
set(MIME_SYSX64 ${CMAKE_BINARY_DIR}/guest/sysx64)
set(MIME_SYSX86 ${CMAKE_BINARY_DIR}/guest/syswow64)
set(MIME_ROM ${CMAKE_BINARY_DIR}/guest/rom)
set(MIME_LIB ${CMAKE_BINARY_DIR}/lib)
set(MIME_TESTS ${CMAKE_BINARY_DIR}/tests)
file(MAKE_DIRECTORY ${MIME_GEN}/x64 ${MIME_GEN}/x86 ${MIME_GEN}/res ${MIME_SYSTEM32} ${MIME_SYSX64} ${MIME_SYSX86}
    ${MIME_LIB}/x64 ${MIME_LIB}/x86 ${MIME_TESTS}/arm64 ${MIME_TESTS}/x64 ${MIME_TESTS}/x86)
