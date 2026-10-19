# the mime command, with the guest files embedded in its rom.

# the rom: every guest file, stripped, page aligned so the guest maps it in place.
get_property(rom_files GLOBAL PROPERTY MIME_ROM_FILES)
add_custom_command(OUTPUT ${MIME_GEN}/rom.S
    COMMAND sh ${CMAKE_SOURCE_DIR}/tools/genrom.sh ${MIME_GEN}/rom.S ${CMAKE_SYSTEM_NAME} ${rom_files}
    DEPENDS tools/genrom.sh ${rom_files} VERBATIM)
add_custom_target(rom DEPENDS ${MIME_GEN}/rom.S)

mime_group(guest ALL DEPENDS rom)

if(NOT MIME_BUILD_HOST)
    return()
endif()

set(host_warnings -Wall -Wextra -Wimplicit-fallthrough -Wno-unused-parameter)
# linux's host code uses gnu extensions.
set(host_gnu ON)
if(APPLE)
    set(host_gnu OFF)
endif()

# the host gpu translates glsl through glslang, and SPIRV-Cross reflects it and writes metal on macos, kept in
# third_party with their licenses.
mime_glob(shaderc_sources third_party/glslang/glslang/GenericCodeGen/*.cpp
    third_party/glslang/glslang/MachineIndependent/*.cpp
    third_party/glslang/glslang/MachineIndependent/preprocessor/*.cpp third_party/glslang/glslang/ResourceLimits/*.cpp
    third_party/glslang/glslang/CInterface/*.cpp third_party/glslang/SPIRV/CInterface/*.cpp)
list(APPEND shaderc_sources third_party/glslang/glslang/OSDependent/Unix/ossource.cpp)
foreach(file GlslangToSpv InReadableOrder Logger SpvBuilder SpvPostProcess SpvTools disassemble doc)
    list(APPEND shaderc_sources third_party/glslang/SPIRV/${file}.cpp)
endforeach()
set(spirv_cross spirv_cfg spirv_cross spirv_cross_c spirv_cross_parsed_ir spirv_cross_util spirv_parser)
if(APPLE)
    list(APPEND spirv_cross spirv_glsl spirv_msl)
endif()
foreach(file IN LISTS spirv_cross)
    list(APPEND shaderc_sources third_party/SPIRV-Cross/${file}.cpp)
endforeach()
add_library(shaderc OBJECT ${shaderc_sources})
target_compile_options(shaderc PRIVATE -w)
if(APPLE)
    target_compile_definitions(shaderc PRIVATE SPIRV_CROSS_C_API_MSL=1 SPIRV_CROSS_C_API_GLSL=1)
endif()
target_include_directories(shaderc PRIVATE third_party/glslang third_party/glslang/glslang third_party)
set_target_properties(shaderc PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)

if(APPLE)
    mime_glob(host_sources host/*.c host/setup/*.c host/vmm/*.c host/ui/*.c host/gpu/*.c host/audio/*.c host/camera/*.c)

    add_executable(mime ${host_sources} ${MIME_GEN}/rom.S $<TARGET_OBJECTS:shaderc>)
    set_source_files_properties(host/gpu/gpu.c host/gpu/shader.c PROPERTIES COMPILE_OPTIONS -fblocks
        INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/third_party;${CMAKE_SOURCE_DIR}/third_party/glslang")
    target_link_libraries(mime PRIVATE "-framework Hypervisor" "-framework AppKit" "-framework QuartzCore"
        "-framework CoreGraphics" "-framework ImageIO" "-framework Metal" "-framework IOSurface" "-framework Carbon"
        "-framework AudioToolbox" "-framework CoreAudio" objc)
    set_target_properties(mime PROPERTIES LINKER_LANGUAGE CXX)
    add_custom_command(TARGET mime POST_BUILD
        COMMAND codesign -s - -f --entitlements ${CMAKE_SOURCE_DIR}/host/mime.entitlements $<TARGET_FILE:mime>
        VERBATIM)
else()
    # kvm, and the linux ui, gpu and audio, in place of the macos frameworks.
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(HOST_LIBS REQUIRED IMPORTED_TARGET sdl3 wayland-client)
    # the desktop bus for the notification area and notifications; without it the guest's icons and balloons are not shown.
    pkg_check_modules(HOST_SDBUS IMPORTED_TARGET libsystemd)
    find_package(Threads REQUIRED)
    mime_glob(host_sources host/*.c host/setup/*.c host/linux/*.c host/vmm/*.c host/camera/*.c)
    list(FILTER host_sources EXCLUDE REGEX "/host/vmm/(vm|vcpu|lazy|loader|window)[.]c$")
    list(APPEND host_sources host/gpu/shader.c)

    # the vulkan wire, generated from the registry.
    add_library(mvk-host OBJECT ${MIME_VK_GEN}/mvk_structs.c ${MIME_VK_GEN}/mvk_host_commands.c)
    target_include_directories(mvk-host PRIVATE third_party/khronos ${MIME_VK_GEN} host/linux include)
    target_compile_options(mvk-host PRIVATE ${host_warnings} -Wno-unused-variable -Wno-unused-function)
    target_compile_definitions(mvk-host PRIVATE _GNU_SOURCE)
    target_link_libraries(mvk-host PRIVATE PkgConfig::HOST_LIBS)
    set_target_properties(mvk-host PROPERTIES C_STANDARD 23 C_EXTENSIONS ON)
    add_dependencies(mvk-host mime-gen-vulkan)

    add_executable(mime ${host_sources} ${MIME_GEN}/rom.S $<TARGET_OBJECTS:mvk-host> $<TARGET_OBJECTS:shaderc>)
    set_source_files_properties(host/gpu/shader.c PROPERTIES
        INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/third_party;${CMAKE_SOURCE_DIR}/third_party/glslang")
    set_source_files_properties(host/linux/gpu.c PROPERTIES COMPILE_OPTIONS -Wno-missing-field-initializers
        INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/third_party/khronos;${MIME_VK_GEN};${CMAKE_SOURCE_DIR}/third_party/glslang")
    set_source_files_properties(host/linux/gamma.c PROPERTIES
        INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/third_party/glslang")
    set_source_files_properties(host/linux/vulkan.c host/linux/vkwsi.c host/linux/video.c host/linux/video_h264.c
        host/linux/video_vp9.c host/linux/video_av1.c PROPERTIES
        COMPILE_OPTIONS -Wno-missing-field-initializers
        INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/third_party/khronos;${MIME_VK_GEN}")
    target_compile_definitions(mime PRIVATE _GNU_SOURCE)
    target_link_libraries(mime PRIVATE PkgConfig::HOST_LIBS Threads::Threads m)
    if(HOST_SDBUS_FOUND)
        target_compile_definitions(mime PRIVATE MIME_HAVE_SDBUS=1)
        target_link_libraries(mime PRIVATE PkgConfig::HOST_SDBUS)
    else()
        message(STATUS "mime: no libsystemd, the host will not show the guest's notification icons")
    endif()
    set_target_properties(mime PROPERTIES LINKER_LANGUAGE CXX)
    add_dependencies(mime mime-gen-vulkan)
endif()

target_include_directories(mime PRIVATE include)
target_compile_options(mime PRIVATE $<$<COMPILE_LANGUAGE:C>:${host_warnings}>)
set_target_properties(mime PROPERTIES C_STANDARD 23 C_STANDARD_REQUIRED ON C_EXTENSIONS ${host_gnu}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
# .incbin is not seen by the dependency scanner.
set_source_files_properties(${MIME_GEN}/rom.S PROPERTIES OBJECT_DEPENDS "${rom_files}")
add_dependencies(mime rom)
