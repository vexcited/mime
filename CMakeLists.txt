# mime build, with llvm: clang, lld-link, llvm-nm, llvm-rc, llvm-strip, llvm-readobj and llvm-dlltool.
#   cmake --preset default && cmake --build --preset default
#
# the guest runs the host's processor: arm64 on macos, x64 on linux x86_64, where the x64 system dlls
# of guest/sysx64 become C:\Windows\System32. one clang builds both sides, so it must be llvm's own.
cmake_minimum_required(VERSION 3.30)

if(NOT DEFINED CMAKE_C_COMPILER AND NOT DEFINED ENV{CC})
    set(CMAKE_C_COMPILER clang)
endif()
if(NOT DEFINED CMAKE_CXX_COMPILER AND NOT DEFINED ENV{CXX})
    set(CMAKE_CXX_COMPILER clang++)
endif()
if(NOT DEFINED CMAKE_ASM_COMPILER AND NOT DEFINED ENV{ASM})
    set(CMAKE_ASM_COMPILER clang)
endif()
# -O2 -g without NDEBUG, as asserts stay on, and assembly without debug information.
set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "build type")
set(CMAKE_C_FLAGS_RELWITHDEBINFO "-O2 -g" CACHE STRING "")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g" CACHE STRING "")
set(CMAKE_ASM_FLAGS_RELWITHDEBINFO "" CACHE STRING "")

project(mime LANGUAGES C CXX ASM)

if(NOT CMAKE_C_COMPILER_ID STREQUAL "Clang" OR CMAKE_C_COMPILER_VERSION VERSION_LESS 19)
    message(FATAL_ERROR "mime needs llvm's clang 19 or later, not ${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}: "
        "apple's clang cannot build windows binaries, put llvm's first in PATH, see README.md")
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_EXTENSIONS OFF)

include(cmake/toolchain.cmake)
include(cmake/guest.cmake)
include(cmake/generated.cmake)
include(cmake/kernel.cmake)
include(cmake/user.cmake)
include(cmake/x86.cmake)
include(cmake/host.cmake)
include(cmake/tests.cmake)
