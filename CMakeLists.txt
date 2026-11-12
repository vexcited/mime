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
set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "build type")
set(CMAKE_C_FLAGS_RELWITHDEBINFO "-O2 -g" CACHE STRING "")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g" CACHE STRING "")
set(CMAKE_ASM_FLAGS_RELWITHDEBINFO "" CACHE STRING "")

project(mime VERSION 0.0.2 LANGUAGES C CXX ASM)
unset(_CMAKE_APPLE_ARCHS_DEFAULT)

if(NOT CMAKE_C_COMPILER_ID STREQUAL "Clang" OR CMAKE_C_COMPILER_VERSION VERSION_LESS 19)
    message(FATAL_ERROR "mime needs llvm's clang 19 or later, not ${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}: "
        "apple's clang cannot build windows binaries, put llvm's first in PATH, see README.md")
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_EXTENSIONS OFF)


# ===================================== toolchain =====================================

foreach(tool lld-link llvm-rc llvm-dlltool)
    string(TOUPPER "${tool}" variable)
    string(REPLACE "-" "_" variable "MIME_${variable}")
    find_program(${variable} ${tool} REQUIRED)
endforeach()

if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
    set(MIME_GUEST_ARCH x64)
else()
    set(MIME_GUEST_ARCH arm64)
endif()

if((APPLE AND MIME_GUEST_ARCH STREQUAL "arm64")
        OR (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND MIME_GUEST_ARCH STREQUAL "x64"))
    set(MIME_BUILD_HOST ON)
else()
    set(MIME_BUILD_HOST OFF)
    message(STATUS "mime: building the ${MIME_GUEST_ARCH} guest only, ${CMAKE_SYSTEM_NAME} ${CMAKE_SYSTEM_PROCESSOR} has no "
        "hypervisor backend for it")
endif()

set(MIME_GEN ${CMAKE_SOURCE_DIR}/gen)
set(MIME_BUILD_GEN ${CMAKE_BINARY_DIR}/gen)
set(MIME_SYSTEM32 ${CMAKE_BINARY_DIR}/guest/system32)
set(MIME_SYSX64 ${CMAKE_BINARY_DIR}/guest/sysx64)
set(MIME_SYSX86 ${CMAKE_BINARY_DIR}/guest/syswow64)
set(MIME_ROM ${CMAKE_BINARY_DIR}/guest/rom)
set(MIME_LIB ${CMAKE_BINARY_DIR}/lib)
file(MAKE_DIRECTORY ${MIME_BUILD_GEN}/res ${MIME_SYSTEM32} ${MIME_SYSX64} ${MIME_SYSX86} ${MIME_LIB}/x64 ${MIME_LIB}/x86)

# ===================================== assets =====================================

# The drawings and sounds that live in their own repositories, mime-icons and mime-audio, and the files of Windows the
# build makes from them and from data/messages. A checkout of a repository next to Mime's is used as it is, so it can be
# edited and built without a commit; otherwise the commit pinned here is fetched. MIME_ICONS_DIR and MIME_AUDIO_DIR
# name another checkout.
include(FetchContent)

set(MIME_ICONS_REPOSITORY https://code.vexcited.com/mime/icons.git)
set(MIME_ICONS_COMMIT ed99f1c9de2bc1b42f7c899b7355503be96cab5f)
set(MIME_AUDIO_REPOSITORY https://code.vexcited.com/mime/audio.git)
set(MIME_AUDIO_COMMIT 86c3138fcab689175f18847b72b06820c7dd0bd6)
set(MIME_ICONS_DIR "" CACHE PATH "A checkout of mime-icons to build from instead of the pinned commit")
set(MIME_AUDIO_DIR "" CACHE PATH "A checkout of mime-audio to build from instead of the pinned commit")

set(MIME_ASSETS ${MIME_BUILD_GEN}/assets)
set(MIME_ASSET_SCRIPTS ${CMAKE_SOURCE_DIR}/scripts/asset/binary.cmake ${CMAKE_SOURCE_DIR}/scripts/asset/raster.cmake
    ${CMAKE_SOURCE_DIR}/scripts/asset/png.cmake)

function(mime_asset_repository out name url commit)
    string(TOUPPER "${name}" upper)
    set(dir "${MIME_${upper}_DIR}")
    if(NOT dir)
        # next to the checkout, or next to the main checkout of a worktree.
        set(roots ${CMAKE_SOURCE_DIR})
        execute_process(COMMAND git rev-parse --path-format=absolute --git-common-dir
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} OUTPUT_VARIABLE common OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE failed ERROR_QUIET)
        if(NOT failed)
            get_filename_component(main "${common}" DIRECTORY)
            list(APPEND roots "${main}")
        endif()
        foreach(root IN LISTS roots)
            get_filename_component(candidate "${root}/../mime-${name}" ABSOLUTE)
            if(IS_DIRECTORY "${candidate}")
                set(dir "${candidate}")
                break()
            endif()
        endforeach()
    endif()
    if(dir)
        message(STATUS "mime-${name}: ${dir}")
    else()
        FetchContent_Declare(mime_${name} GIT_REPOSITORY ${url} GIT_TAG ${commit} GIT_PROGRESS TRUE)
        FetchContent_MakeAvailable(mime_${name})
        set(dir "${mime_${name}_SOURCE_DIR}")
        message(STATUS "mime-${name}: ${commit}")
    endif()
    set(${out} "${dir}" PARENT_SCOPE)
endfunction()

mime_asset_repository(MIME_ICONS icons ${MIME_ICONS_REPOSITORY} ${MIME_ICONS_COMMIT})
mime_asset_repository(MIME_AUDIO audio ${MIME_AUDIO_REPOSITORY} ${MIME_AUDIO_COMMIT})

set(mime_icon_sizes 16 20 24 32 48 256)
set(mime_system_icon_sizes 16 20 24 32 40 48 64 256)
set(mime_cursor_sizes 32 48 64 96 128)
set(mime_messages_kernelbase win32 hresult)
set(mime_messages_ntdll ntstatus)

function(_mime_icon_command output kind images depends)
    add_custom_command(OUTPUT ${output}
        COMMAND ${CMAKE_COMMAND} -DKIND=${kind} -DOUTPUT=${output} "-DIMAGES=${images}" ${ARGN}
            -P ${CMAKE_SOURCE_DIR}/scripts/asset/icon.cmake
        DEPENDS ${depends} ${CMAKE_SOURCE_DIR}/scripts/asset/icon.cmake ${MIME_ASSET_SCRIPTS} VERBATIM)
endfunction()

function(_mime_sized out svg sizes)
    set(images)
    foreach(size IN LISTS sizes)
        list(APPEND images "${svg}@${size}")
    endforeach()
    set(${out} "${images}" PARENT_SCOPE)
endfunction()

# An icon of Windows has its own drawings at small sizes: <name>_<N>.svg draws the sizes up to N above the next
# smaller drawing, <name>.svg the sizes above them all.
function(_mime_icon_images out svg sizes)
    string(REGEX REPLACE "\\.svg$" "" base ${svg})
    file(GLOB smalls CONFIGURE_DEPENDS ${base}_*.svg)
    set(ns)
    foreach(small IN LISTS smalls)
        string(LENGTH "${base}_" skip)
        string(SUBSTRING "${small}" ${skip} -1 tail)
        if(tail MATCHES "^([0-9]+)\\.svg$")
            list(APPEND ns ${CMAKE_MATCH_1})
        endif()
    endforeach()
    list(SORT ns COMPARE NATURAL)
    set(images)
    set(depends ${svg})
    foreach(size IN LISTS sizes)
        set(pick ${svg})
        foreach(n IN LISTS ns)
            if(size LESS_EQUAL n)
                set(pick ${base}_${n}.svg)
                break()
            endif()
        endforeach()
        list(APPEND images "${pick}@${size}")
        list(APPEND depends ${pick})
    endforeach()
    list(REMOVE_DUPLICATES depends)
    set(${out} "${images}" PARENT_SCOPE)
    set(${out}_depends "${depends}" PARENT_SCOPE)
endfunction()

# The file a resource script names as @MIME_ASSETS@/<path>, and the rule that makes it.
function(mime_asset path)
    get_property(done GLOBAL PROPERTY MIME_ASSET_${path})
    if(done)
        return()
    endif()
    set_property(GLOBAL PROPERTY MIME_ASSET_${path} TRUE)
    set(output ${MIME_ASSETS}/${path})
    set(art ${MIME_ICONS})
    if(path MATCHES "^icons/system_(.+)\\.ico$")
        set(svg ${art}/icons/${CMAKE_MATCH_1}.svg)
        _mime_icon_images(images ${svg} "${mime_system_icon_sizes}")
        _mime_icon_command(${output} ico "${images}" "${images_depends}")
    elseif(path MATCHES "^icons/(.+)\\.ico$")
        set(svg ${art}/icons/${CMAKE_MATCH_1}.svg)
        _mime_icon_images(images ${svg} "${mime_icon_sizes}")
        _mime_icon_command(${output} ico "${images}" "${images_depends}")
    elseif(path MATCHES "^cursors/(.+)\\.cur$")
        set(svg ${art}/cursors/${CMAKE_MATCH_1}.svg)
        _mime_sized(images ${svg} "${mime_cursor_sizes}")
        _mime_icon_command(${output} cur "${images}" ${svg})
    elseif(path MATCHES "^cursors/(.+)\\.ani$")
        # a frame is a cursor of its own, made in parallel with the others.
        set(name ${CMAKE_MATCH_1})
        file(GLOB frames CONFIGURE_DEPENDS ${art}/cursors/${name}_[0-9][0-9].svg)
        list(SORT frames)
        if(NOT frames)
            message(FATAL_ERROR "mime-icons has no frames of the cursor ${name}")
        endif()
        set(curs)
        foreach(svg IN LISTS frames)
            get_filename_component(frame ${svg} NAME_WE)
            set(cur ${MIME_ASSETS}/cursors/frames/${frame}.cur)
            _mime_sized(images ${svg} "${mime_cursor_sizes}")
            _mime_icon_command(${cur} cur "${images}" ${svg})
            list(APPEND curs ${cur})
        endforeach()
        list(GET frames 0 first)
        _mime_icon_command(${output} ani "${curs}" "${curs}" -DJIFFIES_FROM=${first})
    elseif(path MATCHES "^bitmaps/(.+)\\.bmp$")
        set(svg ${art}/bitmaps/${CMAKE_MATCH_1}.svg)
        _mime_icon_command(${output} bmp ${svg} ${svg})
    elseif(path MATCHES "^programs/(.+)/(.+)\\.ico$")
        file(GLOB images CONFIGURE_DEPENDS ${art}/programs/${CMAKE_MATCH_1}/${CMAKE_MATCH_2}_*.svg)
        if(NOT images)
            message(FATAL_ERROR "mime-icons has no drawing of ${path}")
        endif()
        _mime_icon_command(${output} ico "${images}" "${images}")
    elseif(path MATCHES "^messages/(.+)\\.bin$")
        set(inputs)
        foreach(list IN LISTS mime_messages_${CMAKE_MATCH_1})
            list(APPEND inputs ${CMAKE_SOURCE_DIR}/data/messages/${list}.txt)
        endforeach()
        add_custom_command(OUTPUT ${output}
            COMMAND ${CMAKE_COMMAND} -DOUTPUT=${output} "-DINPUTS=${inputs}"
                -P ${CMAKE_SOURCE_DIR}/scripts/asset/messages.cmake
            DEPENDS ${inputs} ${CMAKE_SOURCE_DIR}/scripts/asset/messages.cmake
                ${CMAKE_SOURCE_DIR}/scripts/asset/binary.cmake VERBATIM)
    else()
        message(FATAL_ERROR "no rule makes the asset ${path}")
    endif()
endfunction()

# The assets the resource scripts of inputs name, made known to the build, into out.
function(mime_script_assets out)
    set(outputs)
    foreach(input IN LISTS ARGN)
        file(STRINGS ${input} lines REGEX "@MIME_ASSETS@/")
        foreach(line IN LISTS lines)
            string(REGEX MATCHALL "@MIME_ASSETS@/[^\"]+" paths "${line}")
            foreach(path IN LISTS paths)
                string(REPLACE "@MIME_ASSETS@/" "" path "${path}")
                mime_asset(${path})
                list(APPEND outputs ${MIME_ASSETS}/${path})
            endforeach()
        endforeach()
    endforeach()
    set(${out} ${outputs} PARENT_SCOPE)
endfunction()

# The system sounds, C:\Windows\Media, and the General MIDI set, C:\Windows\System32\drivers\gm.dls, for the ROM.
file(GLOB mime_sound_recipes CONFIGURE_DEPENDS ${MIME_AUDIO}/sounds/*.txt)
foreach(recipe IN LISTS mime_sound_recipes)
    get_filename_component(name "${recipe}" NAME_WE)
    set(output "${MIME_ASSETS}/winmedia/${name}.wav")
    add_custom_command(OUTPUT "${output}"
        COMMAND ${CMAKE_COMMAND} "-DINPUT=${recipe}" "-DOUTPUT=${output}" -P ${CMAKE_SOURCE_DIR}/scripts/asset/wave.cmake
        DEPENDS "${recipe}" ${CMAKE_SOURCE_DIR}/scripts/asset/wave.cmake ${MIME_ASSET_SCRIPTS} VERBATIM)
    set_property(GLOBAL APPEND PROPERTY MIME_ROM_FILES "${output}")
endforeach()
add_custom_command(OUTPUT ${MIME_ASSETS}/windrivers/gm.dls
    COMMAND ${CMAKE_COMMAND} -E copy ${MIME_AUDIO}/soundsets/gm.dls ${MIME_ASSETS}/windrivers/gm.dls
    DEPENDS ${MIME_AUDIO}/soundsets/gm.dls VERBATIM)
set_property(GLOBAL APPEND PROPERTY MIME_ROM_FILES ${MIME_ASSETS}/windrivers/gm.dls)

# ===================================== guest =====================================

set(MIME_TRIPLE_x64 --target=x86_64-pc-windows-msvc -march=x86-64 -mcx16)
set(MIME_TRIPLE_arm64 --target=aarch64-pc-windows-msvc -march=armv8.2-a)
set(MIME_TRIPLE_x86 --target=i686-pc-windows-msvc -march=pentium4)
set(MIME_MACHINE_x64 x64)
set(MIME_MACHINE_arm64 arm64)
set(MIME_MACHINE_x86 x86)

set(MIME_GUEST_C_OPTIONS -ffreestanding -nostdlibinc -fms-extensions -fno-stack-protector -gdwarf
    -fmacro-prefix-map=${CMAKE_SOURCE_DIR}/=
    -Wall -Wextra -Wimplicit-fallthrough -Wno-unused-parameter -Wno-missing-field-initializers)
set(MIME_GUEST_CXX_OPTIONS -fno-threadsafe-statics -fexceptions -fcxx-exceptions)

function(mime_flavor name arch)
    add_library(mime-flags-${name} INTERFACE)
    target_compile_options(mime-flags-${name} INTERFACE ${MIME_TRIPLE_${arch}}
        "$<$<COMPILE_LANGUAGE:C,CXX>:${MIME_GUEST_C_OPTIONS};${ARGN}>"
        "$<$<COMPILE_LANGUAGE:CXX>:${MIME_GUEST_CXX_OPTIONS}>")
    target_include_directories(mime-flags-${name} INTERFACE include guest/include guest/rtl ${MIME_GEN})
    set(MIME_ARCH_${name} ${arch} PARENT_SCOPE)
endfunction()

if(MIME_GUEST_ARCH STREQUAL "x64")
    # drivers' exceptions unwind through the kernel's frames, so the kernel has unwind data like windows'.
    mime_flavor(kernel x64 -fasynchronous-unwind-tables)
else()
    mime_flavor(kernel ${MIME_GUEST_ARCH})
endif()
mime_flavor(arm64 arm64 -fasynchronous-unwind-tables)
if(MIME_GUEST_ARCH STREQUAL "x64")
    mime_flavor(x64 x64 -fasynchronous-unwind-tables -DMIME_X64_NATIVE=1)
else()
    mime_flavor(x64 x64 -fasynchronous-unwind-tables)
endif()
mime_flavor(x86 x86)

function(mime_objects target flavor)
    cmake_parse_arguments(PARSE_ARGV 2 arg "" "" "SOURCES;OPTIONS;INCLUDES;DEPENDS")
    add_library(${target} OBJECT EXCLUDE_FROM_ALL ${arg_SOURCES})
    mime_only_targets(depends ${arg_DEPENDS})
    target_link_libraries(${target} PRIVATE mime-flags-${flavor})
    if(arg_OPTIONS)
        target_compile_options(${target} PRIVATE "$<$<COMPILE_LANGUAGE:C,CXX>:${arg_OPTIONS}>")
    endif()
    if(arg_INCLUDES)
        target_include_directories(${target} PRIVATE ${arg_INCLUDES})
    endif()
    set_target_properties(${target} PROPERTIES C_STANDARD 23 C_STANDARD_REQUIRED ON CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON)
    if(depends)
        add_dependencies(${target} ${depends})
    endif()
endfunction()

function(mime_object_list out)
    set(list)
    foreach(spec IN LISTS ARGN)
        if(spec MATCHES "^([^:]+):(.+)$")
            string(REPLACE "." "[.]" source "${CMAKE_MATCH_2}")
            list(APPEND list "$<FILTER:$<TARGET_OBJECTS:${CMAKE_MATCH_1}>,INCLUDE,/${source}[.]o$>")
        else()
            list(APPEND list "$<TARGET_OBJECTS:${spec}>")
        endif()
    endforeach()
    set(${out} ${list} PARENT_SCOPE)
endfunction()

function(mime_object_targets out)
    list(TRANSFORM ARGN REPLACE ":.*$" "")
    list(REMOVE_DUPLICATES ARGN)
    set(${out} ${ARGN} PARENT_SCOPE)
endfunction()

function(mime_only_targets out)
    set(list)
    foreach(item IN LISTS ARGN)
        if(TARGET ${item})
            list(APPEND list ${item})
        endif()
    endforeach()
    set(${out} ${list} PARENT_SCOPE)
endfunction()

function(mime_resource name arch)
    get_property(done GLOBAL PROPERTY MIME_RESOURCE_${arch}_${name})
    if(done)
        return()
    endif()
    set_property(GLOBAL PROPERTY MIME_RESOURCE_${arch}_${name} TRUE)
    string(TOLOWER ${name} lower)
    set(version ${MIME_GEN}/version/${lower}.rc)
    if(arch STREQUAL "x86" AND EXISTS ${MIME_GEN}/version/x86/${lower}.rc)
        set(version ${MIME_GEN}/version/x86/${lower}.rc)
    endif()
    set(base ${MIME_BUILD_GEN}/res/${MIME_MACHINE_${arch}}/${name})
    set(inputs ${version} ${ARGN})
    # the files a script includes, such as the type libraries of the shell dlls, and the icons, cursors and message
    # tables the build makes, see the assets section above.
    file(GLOB includes CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/guest/*/*.tlb)
    mime_script_assets(assets ${inputs})
    list(APPEND includes ${assets})
    add_custom_command(OUTPUT ${base}.res
        COMMAND ${CMAKE_COMMAND} -E make_directory ${MIME_BUILD_GEN}/res/${MIME_MACHINE_${arch}}
        COMMAND ${CMAKE_COMMAND} -DOUTPUT=${base}.rc "-DINPUTS=${inputs}" -DROOT=${CMAKE_SOURCE_DIR} -DASSETS=${MIME_ASSETS}
            -P ${CMAKE_SOURCE_DIR}/scripts/concat.cmake
        COMMAND ${MIME_LLVM_RC} -no-preprocess -c 1252 -fo ${base}.res -- ${base}.rc
        DEPENDS ${inputs} ${includes} scripts/concat.cmake VERBATIM)
endfunction()

function(mime_arch_script out prefix arch)
    set(script)
    if(EXISTS ${prefix}_${arch}.rc)
        set(script ${prefix}_${arch}.rc)
    elseif(arch STREQUAL "arm64" AND EXISTS ${prefix}_x64.rc)
        set(script ${prefix}_x64.rc)
    endif()
    set(${out} ${script} PARENT_SCOPE)
endfunction()

set(MIME_LINK_FLAGS /nologo /debug:dwarf /build-id /nodefaultlib /opt:ref /incremental:no /filealign:0x1000
    /version:10.0 /osversion:10.0 /Brepro /release)

function(mime_link output arch)
    cmake_parse_arguments(PARSE_ARGV 2 arg "" "IMPLIB;RESOURCE;RESOURCE_SCRIPT;IMPORTS;ROM"
        "OBJECTS;LIBS;FLAGS;DEPENDS;GROUPS")
    mime_object_list(objects ${arg_OBJECTS})
    mime_object_targets(object_targets ${arg_OBJECTS})
    set(flags /machine:${MIME_MACHINE_${arch}} ${MIME_LINK_FLAGS} ${arg_FLAGS})
    if(arch STREQUAL "x86")
        list(APPEND flags /safeseh:no)
    endif()
    set(rom_flags ${flags})
    list(REMOVE_ITEM rom_flags /debug:dwarf)
    list(FILTER rom_flags EXCLUDE REGEX "^/map:")
    list(APPEND rom_flags /noimplib)
    set(outputs ${output})
    set(post)
    if(arg_IMPLIB AND NOT arg_IMPORTS)
        list(APPEND flags /implib:${arg_IMPLIB})
        list(APPEND outputs ${arg_IMPLIB})
    elseif(arg_IMPLIB)
        list(APPEND outputs ${arg_IMPLIB})
        list(APPEND flags /noimplib)
        set(post COMMAND ${MIME_LLVM_DLLTOOL} -m i386 -k -d ${arg_IMPORTS} -l ${arg_IMPLIB})
    endif()
    set(resource)
    if(arg_RESOURCE)
        mime_resource(${arg_RESOURCE} ${arch} ${arg_RESOURCE_SCRIPT})
        set(resource ${MIME_BUILD_GEN}/res/${MIME_MACHINE_${arch}}/${arg_RESOURCE}.res)
    endif()
    set(depends ${objects} ${object_targets} ${arg_LIBS} ${resource} ${arg_DEPENDS})
    get_filename_component(directory ${output} DIRECTORY)
    file(RELATIVE_PATH comment ${CMAKE_BINARY_DIR} ${output})
    add_custom_command(OUTPUT ${outputs}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${directory}
        COMMAND ${MIME_LLD_LINK} ${flags} /out:${output} ${objects} ${arg_LIBS} ${resource}
        ${post}
        DEPENDS ${depends}
        COMMENT "Linking ${comment}" COMMAND_EXPAND_LISTS VERBATIM)
    if(arg_ROM)
        get_filename_component(directory ${arg_ROM} DIRECTORY)
        file(RELATIVE_PATH comment ${CMAKE_BINARY_DIR} ${arg_ROM})
        add_custom_command(OUTPUT ${arg_ROM}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${directory}
            COMMAND ${MIME_LLD_LINK} ${rom_flags} /out:${arg_ROM} ${objects} ${arg_LIBS} ${resource}
            DEPENDS ${depends}
            COMMENT "Linking ${comment}" COMMAND_EXPAND_LISTS VERBATIM)
        set_property(GLOBAL APPEND PROPERTY MIME_ROM_FILES ${arg_ROM})
    endif()
    foreach(group IN LISTS arg_GROUPS)
        set_property(GLOBAL APPEND PROPERTY MIME_GROUP_${group} ${output})
    endforeach()
endfunction()

function(mime_group name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "ALL" "" "DEPENDS")
    get_property(files GLOBAL PROPERTY MIME_GROUP_${name})
    set(all)
    if(arg_ALL)
        set(all ALL)
    endif()
    add_custom_target(${name} ${all} DEPENDS ${files} ${arg_DEPENDS})
endfunction()

function(mime_glob out)
    set(files)
    foreach(pattern IN LISTS ARGN)
        file(GLOB found CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/${pattern})
        list(SORT found)
        list(APPEND files ${found})
    endforeach()
    set(${out} ${files} PARENT_SCOPE)
endfunction()

# ===================================== kernel =====================================

set(ntos_options -DMIME_KERNEL=1)
mime_glob(rtl_sources guest/rtl/*.c)
mime_glob(crt_sources guest/crt/*.c)
if(MIME_GUEST_ARCH STREQUAL "x64")
    mime_glob(crt_kernel_asm guest/crt/x64/*.S)
else()
    mime_glob(crt_kernel_asm guest/crt/*.S)
endif()
mime_glob(ntos_asm guest/ntos/arch/${MIME_GUEST_ARCH}/*.S)
mime_glob(ntos_common guest/ntos/*.c guest/ntos/*/*.c)
list(FILTER ntos_common EXCLUDE REGEX "/guest/ntos/arch/")
mime_glob(ntos_arch guest/ntos/arch/${MIME_GUEST_ARCH}/*.c)

mime_objects(kernel-ntos kernel SOURCES ${ntos_common} ${ntos_arch} ${ntos_asm} guest/lib/crypto/hash.c
    OPTIONS ${ntos_options} INCLUDES guest/ntos guest/lib)
mime_objects(kernel-rtl kernel SOURCES ${rtl_sources} OPTIONS ${ntos_options} INCLUDES guest/ntos)
mime_objects(kernel-crt kernel SOURCES ${crt_sources} ${crt_kernel_asm} OPTIONS -fno-builtin)
mime_objects(kernel-services kernel SOURCES ${MIME_GEN}/ntos_services.c OPTIONS ${ntos_options} -Wno-unused-function
    INCLUDES guest/ntos)

set(MIME_NTOSKRNL_LIB ${MIME_LIB}/ntoskrnl.lib)
mime_link(${MIME_SYSTEM32}/ntoskrnl.exe ${MIME_GUEST_ARCH}
    OBJECTS kernel-ntos kernel-rtl kernel-crt kernel-services
    FLAGS /subsystem:native,10.0 /driver /entry:KiSystemStartup /base:0xFFFFF80000200000 /stack:0x80000,0x2000
        /def:${MIME_GEN}/${MIME_GUEST_ARCH}/ntoskrnl.def /map:${CMAKE_BINARY_DIR}/guest/ntoskrnl.map
    DEPENDS ${MIME_GEN}/${MIME_GUEST_ARCH}/ntoskrnl.def IMPLIB ${MIME_NTOSKRNL_LIB} RESOURCE ntoskrnl.exe
    ROM ${MIME_ROM}/ntoskrnl.exe GROUPS guest)

mime_glob(win32k_sources guest/win32k/*.c guest/win32k/*/*.c guest/lib/ttf/*.c guest/lib/png/*.c guest/lib/msstyles/*.c)
mime_objects(kernel-win32k kernel SOURCES ${win32k_sources} ${MIME_GEN}/win32k_services.c
    INCLUDES guest/win32k guest/lib third_party/khronos)
mime_link(${MIME_SYSTEM32}/win32k.sys ${MIME_GUEST_ARCH} OBJECTS kernel-win32k LIBS ${MIME_NTOSKRNL_LIB}
    FLAGS /subsystem:native,10.0 /driver /entry:DriverEntry /base:0xFFFFF80040000000 /stack:0x40000,0x1000
        /map:${CMAKE_BINARY_DIR}/guest/win32k.map
    RESOURCE win32k.sys ROM ${MIME_ROM}/win32k.sys GROUPS guest)

# the filter manager, which drivers import from System32\drivers\fltmgr.sys.
mime_glob(fltmgr_sources guest/fltmgr/*.c)
mime_objects(kernel-fltmgr kernel SOURCES ${fltmgr_sources} INCLUDES guest/fltmgr)
mime_link(${MIME_SYSTEM32}/drivers/fltmgr.sys ${MIME_GUEST_ARCH} OBJECTS kernel-fltmgr LIBS ${MIME_NTOSKRNL_LIB}
    FLAGS /subsystem:native,10.0 /driver /entry:DriverEntry /def:${CMAKE_SOURCE_DIR}/guest/fltmgr/fltmgr.def
        /map:${CMAKE_BINARY_DIR}/guest/fltmgr.map
    DEPENDS ${CMAKE_SOURCE_DIR}/guest/fltmgr/fltmgr.def ${MIME_NTOSKRNL_LIB} IMPLIB ${MIME_LIB}/fltmgr.lib
    ROM ${MIME_ROM}/windrivers/fltmgr.sys GROUPS guest)

# ===================================== user =====================================

if(MIME_GUEST_ARCH STREQUAL "arm64")
    set(MIME_USER_ARCHS arm64 x64)
else()
    set(MIME_USER_ARCHS x64)
endif()

set(MIME_OUT_arm64 ${MIME_SYSTEM32})
set(MIME_OUT_x64 ${MIME_SYSX64})
set(MIME_OUT_x86 ${MIME_SYSX86})
set(MIME_LIB_arm64 ${MIME_LIB})
set(MIME_LIB_x64 ${MIME_LIB}/x64)
set(MIME_LIB_x86 ${MIME_LIB}/x86)
set(MIME_ROM_arm64 ${MIME_ROM})
if(MIME_GUEST_ARCH STREQUAL "x64")
    set(MIME_ROM_x64 ${MIME_ROM})
else()
    set(MIME_ROM_x64 ${MIME_ROM}/sysx64)
endif()
set(MIME_ROM_x86 ${MIME_ROM}/syswow64)
set(MIME_DLL_FLAGS_arm64 /dll /subsystem:console,10.0 /highentropyva /dynamicbase /stack:0x40000,0x1000)
set(MIME_DLL_FLAGS_x64 ${MIME_DLL_FLAGS_arm64})
set(MIME_DLL_FLAGS_x86 /dll /subsystem:console,6.0 /dynamicbase /stack:0x40000,0x1000)

# the calls user32 exports by ordinal only, the dpi ones of Windows 11. GetWindowDPI has a name here, too.
set(user32_ordinals ForceWindowToDpiForTest:2642 RestoreWindowDpiChanges:2643 DeferWindowDpiChanges:2644 SetDpiForWindow:2661
    EnableChildWindowDpiMessage:2704 IsChildWindowDpiMessageEnabled:2705 IsWindowBroadcastingDpiToChildren:2706)
set(user32_args_x86_ForceWindowToDpiForTest 8)
set(user32_args_x86_RestoreWindowDpiChanges 4)
set(user32_args_x86_DeferWindowDpiChanges 4)
set(user32_args_x86_SetDpiForWindow 8)
set(user32_args_x86_EnableChildWindowDpiMessage 8)
set(user32_args_x86_IsChildWindowDpiMessageEnabled 4)
set(user32_args_x86_IsWindowBroadcastingDpiToChildren 4)
foreach(arch IN ITEMS x64 arm64 x86)
    set(MIME_USER32_ORDINALS_${arch} /export:GetWindowDPI,@2707)
    foreach(entry IN LISTS user32_ordinals)
        string(REPLACE ":" ";" entry "${entry}")
        list(GET entry 0 name)
        list(GET entry 1 ordinal)
        if(arch STREQUAL "x86")
            list(APPEND MIME_USER32_ORDINALS_${arch} /export:${name}=_${name}@${user32_args_x86_${name}},@${ordinal},NONAME)
        else()
            list(APPEND MIME_USER32_ORDINALS_${arch} /export:${name},@${ordinal},NONAME)
        endif()
    endforeach()
endforeach()
# x86 has the name of GetWindowDPI in its definition file already, with the ordinal.
list(REMOVE_ITEM MIME_USER32_ORDINALS_x86 /export:GetWindowDPI,@2707)

set(MIME_CHKSTK_arm64 guest/crt/chkstk.S)
set(MIME_CHKSTK_x64 guest/crt/x64/chkstk.S)
set(MIME_CHKSTK_x86 guest/crt/x86/chkstk.S)
foreach(arch IN LISTS MIME_USER_ARCHS ITEMS x86)
    mime_objects(${arch}-rtl ${arch} SOURCES ${rtl_sources})
    mime_objects(${arch}-crt ${arch} SOURCES ${crt_sources} OPTIONS -fno-builtin)
    mime_objects(${arch}-chkstk ${arch} SOURCES ${MIME_CHKSTK_${arch}})
    set(lib_includes guest/lib third_party/SPIRV-Cross)
    foreach(lib privdata dxbc dxil hlsl png pngio jpeg tiff gif bcn vector msstyles compound cabinet audio dinput xinput richedit crypto ttf xmldom cbs lzms msdelta winrt icc par)
        mime_glob(lib_sources guest/lib/${lib}/*.c)
        mime_objects(${arch}-lib-${lib} ${arch} SOURCES ${lib_sources} INCLUDES ${lib_includes})
    endforeach()
    # damaged jpeg xr data can overflow the integer transforms, which must wrap.
    mime_glob(lib_sources guest/lib/jxr/*.c)
    mime_objects(${arch}-lib-jxr ${arch} SOURCES ${lib_sources} INCLUDES ${lib_includes} OPTIONS -fwrapv)
endforeach()
mime_objects(x86-div x86 SOURCES guest/crt/x86/div.c guest/crt/x86/div_entry.S)
set(MIME_RUNTIME_arm64 arm64-rtl arm64-crt arm64-chkstk)
set(MIME_RUNTIME_x64 x64-rtl x64-crt x64-chkstk)
set(MIME_RUNTIME_x86 x86-rtl x86-crt x86-chkstk x86-div)

function(mime_dll name arch)
    cmake_parse_arguments(PARSE_ARGV 2 arg "NOIMPLIB" "BASE;FILE;ENTRY;DEF;IMPORTS_DEF"
        "SOURCES;OBJECTS;OPTIONS;INCLUDES;IMPORTS;LIBS;FLAGS;DEPENDS")
    if(NOT arg_FILE)
        set(arg_FILE ${name}.dll)
    endif()
    if(NOT arg_ENTRY)
        set(arg_ENTRY /entry:DllMain)
        if(arch STREQUAL "x86")
            set(arg_ENTRY /entry:DllMain@12)
        endif()
    endif()
    set(objects ${arg_OBJECTS})
    if(arg_SOURCES)
        mime_only_targets(target_depends ${arg_DEPENDS})
        mime_objects(${arch}-${name} ${arch} SOURCES ${arg_SOURCES} OPTIONS ${arg_OPTIONS}
            INCLUDES guest/${name} ${arg_INCLUDES} DEPENDS ${target_depends})
        list(PREPEND objects ${arch}-${name})
    endif()
    set(libs)
    foreach(import IN LISTS arg_IMPORTS)
        list(APPEND libs ${MIME_LIB_${arch}}/${import}.lib)
    endforeach()
    list(APPEND libs ${arg_LIBS})
    set(output ${MIME_OUT_${arch}}/${arg_FILE})
    set(flags ${MIME_DLL_FLAGS_${arch}} ${arg_ENTRY} /base:${arg_BASE} ${arg_FLAGS})
    set(implib ${MIME_LIB_${arch}}/${name}.lib)
    set(depends)
    foreach(item IN LISTS arg_DEPENDS)
        if(NOT TARGET ${item})
            list(APPEND depends ${item})
        endif()
    endforeach()
    set(script)
    if(EXISTS ${CMAKE_SOURCE_DIR}/guest/${name}/${name}.rc)
        set(script RESOURCE_SCRIPT ${CMAKE_SOURCE_DIR}/guest/${name}/${name}.rc)
    endif()
    # the icons, cursors, bitmaps and message tables the libraries carry, named by the scripts of gen/resources.
    if(NOT script AND EXISTS ${MIME_GEN}/resources/${name}.rc)
        set(script RESOURCE_SCRIPT ${MIME_GEN}/resources/${name}.rc)
    endif()
    # a library of types that is laid out for the processor of the dll.
    mime_arch_script(arch_script ${CMAKE_SOURCE_DIR}/guest/${name}/${name} ${arch})
    if(arch_script)
        set(script RESOURCE_SCRIPT ${arch_script})
    endif()
    if(arch STREQUAL "x86")
        set(def ${arg_DEF})
        set(imports_def ${arg_IMPORTS_DEF})
        if(NOT def)
            set(def ${MIME_GEN}/x86/${name}.def)
            set(imports_def ${MIME_GEN}/x86/${name}.imports.def)
        endif()
        if(arg_NOIMPLIB)
            mime_link(${output} x86 OBJECTS ${objects} LIBS ${libs} FLAGS ${flags} /def:${def} /noimplib
                DEPENDS ${def} ${depends} RESOURCE ${arg_FILE} ${script} ROM ${MIME_ROM_x86}/${arg_FILE} GROUPS guest)
        else()
            mime_link(${output} x86 OBJECTS ${objects} LIBS ${libs} FLAGS ${flags} /def:${def}
                DEPENDS ${def} ${imports_def} ${depends} IMPLIB ${implib} IMPORTS ${imports_def}
                RESOURCE ${arg_FILE} ${script} ROM ${MIME_ROM_x86}/${arg_FILE} GROUPS guest)
        endif()
    else()
        if(NOT arg_DEF AND EXISTS ${CMAKE_SOURCE_DIR}/guest/${name}/${name}.def)
            set(arg_DEF ${CMAKE_SOURCE_DIR}/guest/${name}/${name}.def)
        endif()
        if(arg_DEF)
            list(APPEND flags /def:${arg_DEF})
            list(APPEND depends ${arg_DEF})
        endif()
        if(arg_NOIMPLIB)
            mime_link(${output} ${arch} OBJECTS ${objects} LIBS ${libs} FLAGS ${flags} /noimplib DEPENDS ${depends}
                RESOURCE ${arg_FILE} ${script} ROM ${MIME_ROM_${arch}}/${arg_FILE} GROUPS guest)
        else()
            mime_link(${output} ${arch} OBJECTS ${objects} LIBS ${libs} FLAGS ${flags} DEPENDS ${depends}
                IMPLIB ${implib} RESOURCE ${arg_FILE} ${script} ROM ${MIME_ROM_${arch}}/${arg_FILE} GROUPS guest)
        endif()
    endif()
endfunction()

function(mime_sources out name arch)
    if(arch STREQUAL "x86")
        mime_glob(files guest/${name}/*.c guest/${name}/*.cpp guest/${name}/x86/*.c guest/${name}/x86/*.S)
    else()
        mime_glob(files guest/${name}/*.c guest/${name}/*.cpp guest/${name}/${arch}/*.S)
        if(arch STREQUAL "arm64")
            mime_glob(top guest/${name}/*.S)
            list(APPEND files ${top})
        endif()
    endif()
    set(${out} ${files} PARENT_SCOPE)
endfunction()

set(MIME_SYSCALLS_arm64 ${MIME_GEN}/ntdll_syscalls.S)
set(MIME_SYSCALLS_x64 ${MIME_GEN}/ntdll_syscalls_x64.S)
set(MIME_WIN32K_SYSCALLS_arm64 ${MIME_GEN}/win32u_syscalls.S)
set(MIME_WIN32K_SYSCALLS_x64 ${MIME_GEN}/win32u_syscalls_x64.S)

foreach(arch IN LISTS MIME_USER_ARCHS)
    mime_objects(${arch}-ntsyscalls ${arch} SOURCES ${MIME_SYSCALLS_${arch}})
    mime_objects(${arch}-win32ksyscalls ${arch} SOURCES ${MIME_WIN32K_SYSCALLS_${arch}})

    if(arch STREQUAL "x64")
        mime_glob(ntdll_sources guest/ntdll/*.c guest/ntdll/x64/*.c guest/ntdll/x64/*.S)
    else()
        mime_glob(ntdll_sources guest/ntdll/*.c guest/ntdll/arm64/*.c guest/ntdll/arm64/*.S)
    endif()
    mime_objects(${arch}-ntdll ${arch} SOURCES ${ntdll_sources} guest/lib/crypto/hash.c INCLUDES guest/ntdll guest/lib)
    set(ntdll_objects ${arch}-ntdll ${arch}-ntsyscalls ${MIME_RUNTIME_${arch}})
    mime_dll(ntdll ${arch} BASE 0x7FFC00000000 ENTRY /noentry OBJECTS ${ntdll_objects}
        DEF ${MIME_GEN}/${arch}/ntdll.def)

    mime_dll(win32u ${arch} BASE 0x7FF700000000 ENTRY /noentry OBJECTS ${arch}-win32ksyscalls
        FLAGS /subsystem:windows,10.0 DEF ${MIME_GEN}/win32u.def)

    mime_sources(sources kernelbase ${arch})
    mime_dll(kernelbase ${arch} BASE 0x7FFB00000000 SOURCES ${sources} OBJECTS ${arch}-chkstk
        OPTIONS -DMIME_BUILD_KERNELBASE IMPORTS ntdll)

    mime_objects(${arch}-kernel32-thunks ${arch} SOURCES ${MIME_GEN}/${arch}/kernel32_thunks.S)
    mime_sources(sources kernel32 ${arch})
    mime_dll(kernel32 ${arch} BASE 0x7FFA00000000 SOURCES ${sources} OBJECTS ${arch}-chkstk ${arch}-kernel32-thunks
        OPTIONS -DMIME_BUILD_KERNEL32 IMPORTS ntdll kernelbase DEF ${MIME_GEN}/${arch}/kernel32.def)

    mime_dll(mscoree ${arch} BASE 0x7FF7B0000000 SOURCES guest/mscoree/main.c guest/kernelbase/features.c guest/mscoree/${arch}/common.S ${MIME_GEN}/${arch}/mscoree_thunks.S OBJECTS ${arch}-chkstk
        IMPORTS ntdll kernel32 DEF ${MIME_GEN}/${arch}/mscoree.def)

    mime_sources(sources gdi32 ${arch})
    mime_dll(gdi32 ${arch} BASE 0x7FF800000000 SOURCES ${sources} guest/kernelbase/features.c OBJECTS ${arch}-chkstk
        OPTIONS -DMIME_BUILD_GDI32 IMPORTS ntdll kernel32 win32u)

    mime_sources(sources user32 ${arch})
    mime_dll(user32 ${arch} BASE 0x7FF900000000 SOURCES ${sources}
        OBJECTS ${arch}-chkstk ${arch}-lib-png OPTIONS -DMIME_BUILD_USER32 INCLUDES guest/lib
        IMPORTS ntdll kernel32 win32u gdi32 FLAGS /subsystem:windows,10.0 ${MIME_USER32_ORDINALS_${arch}})

    # the icons of the system, found by their numbers in the way Windows 11 keeps them.
    mime_dll(imageres ${arch} BASE 0x7FF7D0000000 ENTRY /noentry NOIMPLIB DEF ${CMAKE_SOURCE_DIR}/guest/imageres/imageres.def)
endforeach()

if(MIME_GUEST_ARCH STREQUAL "arm64")
    mime_glob(sources guest/xta64/*.c guest/xta64/*.S)
    mime_dll(xta64 arm64 BASE 0x7FFD00000000 ENTRY /noentry SOURCES ${sources}
        OBJECTS arm64-ntsyscalls ${MIME_RUNTIME_arm64} OPTIONS -fno-math-errno
        FLAGS /export:RtlUserThreadStart=MxUserThreadStart NOIMPLIB)
endif()

set(MIME_EXTRA_DLLS msvcrt:0x7FF6F0000000 advapi32:0x7FF6E0000000 sechost:0x7FF080000000 ws2_32:0x7FF6D0000000 iphlpapi:0x7FF6C0000000
    dnsapi:0x7FF6B0000000 ole32:0x7FF6A0000000 oleaut32:0x7FF690000000 rpcrt4:0x7FF680000000
    shell32:0x7FF670000000 shlwapi:0x7FF660000000 comdlg32:0x7FF650000000 comctl32:0x7FF640000000
    crypt32:0x7FF630000000 schannel:0x7FF620000000 secur32:0x7FF610000000 uxtheme:0x7FF600000000
    imm32:0x7FF5F0000000 usp10:0x7FF5E0000000 dwmapi:0x7FF5D0000000 winmm:0x7FF5C0000000
    version:0x7FF5B0000000 userenv:0x7FF5A0000000 wtsapi32:0x7FF590000000 netapi32:0x7FF580000000
    odbc32:0x7FF570000000 winspool:0x7FF560000000 msi:0x7FF550000000 dbghelp:0x7FF540000000 imagehlp:0x7FF100000000 mswsock:0x7FF720000000 winsta:0x7FF730000000
    bcrypt:0x7FF530000000 avrt:0x7FF520000000 dinput8:0x7FF510000000 dwrite:0x7FF500000000
    dxgi:0x7FF4F0000000 wsock32:0x7FF4E0000000 ucrtbase:0x7FF4D0000000 mpr:0x7FF4C0000000 wintrust:0x7FF4B0000000
    mmdevapi:0x7FF4A0000000 gdiplus:0x7FF490000000 wininet:0x7FF480000000 urlmon:0x7FF470000000
    msimg32:0x7FF460000000 oleacc:0x7FF450000000 oledlg:0x7FF440000000 atl:0x7FEC70000000 combase:0x7FF430000000
    d3d11:0x7FF420000000 msvcp_win:0x7FF410000000 d3dcompiler_47:0x7FF400000000 d3d12:0x7FF3E0000000
    d3d9:0x7FF3D0000000 cfgmgr32:0x7FF2F0000000 setupapi:0x7FF3C0000000 d3d10core:0x7FF3B0000000 d3d10_1core:0x7FF3A0000000
    d3d10:0x7FF390000000 d3d10_1:0x7FF380000000 msacm32:0x7FF370000000 dsound:0x7FF320000000
    ddraw:0x7FF310000000 dinput:0x7FF300000000 hid:0x7FF2E0000000 xinput1_4:0x7FF2D0000000
    xinput1_3:0x7FF2C0000000 xinput1_2:0x7FF2B0000000 xinput1_1:0x7FF2A0000000 xinput9_1_0:0x7FF290000000
    winhttp:0x7FF240000000 windows_storage:0x7FF130000000 explorerframe:0x7FF120000000 ieframe:0x7FEC00000000
    propsys:0x7FF230000000 shcore:0x7FF220000000 powrprof:0x7FF210000000 bcryptprimitives:0x7FF200000000
    pdh:0x7FF1F0000000 wevtapi:0x7FF1E0000000 tbs:0x7FF1D0000000 ncrypt:0x7FF1C0000000 credui:0x7FF1B0000000
    cryptui:0x7FF1A0000000 ndfapi:0x7FF190000000 winusb:0x7FF180000000 bthprops:0x7FF170000000
    dhcpcsvc:0x7FF160000000 esent:0x7FF150000000 uiautomationcore:0x7FF140000000
    msctf:0x7FF0D0000000 riched20:0x7FF0C0000000 msftedit:0x7FF0B0000000 riched32:0x7FF0A0000000
    dcomp:0x7FF030000000 prntvpt:0x7FF020000000 psapi:0x7FF0E0000000 lz32:0x7FF0F0000000
    activeds:0x7FF070000000 advpack:0x7FF110000000 mfplat:0x7FF060000000 mf:0x7FF050000000 mfreadwrite:0x7FF040000000 mfcaptureengine:0x7FF010000000 coremessaging:0x7FF090000000
    cabinet:0x7FF740000000 sfc:0x7FF750000000 sfc_os:0x7FF760000000 msxml3:0x7FF780000000 msxml6:0x7FF790000000
    windowscodecs:0x7FF7A0000000 vbscript:0x7FF770000000 jscript:0x7FF7C0000000
    onecorecommonproxystub:0x7FEFF0000000 onecoreuapcommonproxystub:0x7FEFE0000000
    rpcss:0x7FEFD0000000 rpcepmap:0x7FEFC0000000 wkscli:0x7FEE70000000 wkssvc:0x7FEE00000000 httpapi:0x7FEDE0000000 wbemcore:0x7FEDD0000000 loadperf:0x7FEDC0000000 webservices:0x7FEDA0000000 xolehlp:0x7FED90000000 vssapi:0x7FED80000000 xmllite:0x7FED70000000 d2d1:0x7FED20000000 mstask:0x7FED10000000 msado15:0x7FED00000000 scrrun:0x7FECF0000000 wshom:0x7FECE0000000 security:0x7FED60000000 sspicli:0x7FECC0000000 rsaenh:0x7FEC90000000 profapi:0x7FEC80000000 clusapi:0x7FED50000000 resutils:0x7FED40000000 nsisvc:0x7FEDF0000000 keyiso:0x7FEDB0000000 feclient:0x7FEFB0000000 zipfldr:0x7FEFA0000000 avifil32:0x7FEF90000000 glu32:0x7FEF80000000 odbccp32:0x7FEE10000000 mscms:0x7FEE20000000 mapi32:0x7FEE30000000 hlink:0x7FEE40000000 authz:0x7FEE50000000 wldap32:0x7FEE60000000 ntdsapi:0x7FEE80000000 mlang:0x7FED30000000 query:0x7FEE90000000 fontsub:0x7FEEA0000000 tdh:0x7FEEB0000000 windows.networking:0x7FEEC0000000 dbgeng:0x7FEED0000000 wmvcore:0x7FEEE0000000 avicap32:0x7FEEF0000000 rstrtmgr:0x7FEF00000000 wlanapi:0x7FEF10000000 snmpapi:0x7FEF20000000 ktmw32:0x7FEF30000000 msvfw32:0x7FEF40000000 normaliz:0x7FEF50000000 cryptsp:0x7FEF60000000 shfolder:0x7FEF70000000 srvcli:0x7FEC40000000 netutils:0x7FEC30000000)
list(APPEND MIME_EXTRA_DLLS wldp:0x7FECD0000000 rasapi32:0x7FEC10000000 uianimation:0x7FECB0000000
    windows.graphics:0x7FECA0000000 cryptnet:0x7FEC20000000 msdmo:0x7FEC30000000 dxva2:0x7FEC40000000)

foreach(arch IN LISTS MIME_USER_ARCHS)
    set(objects_msi ${arch}-lib-compound ${arch}-lib-cabinet ${arch}-lib-crypto:guest/lib/crypto/hash.c)
    set(includes_msi guest/lib)
    set(libs_sfc_os ${MIME_LIB_${arch}}/advapi32.lib ${MIME_LIB_${arch}}/version.lib)
    set(objects_cabinet ${arch}-lib-cabinet)
    foreach(msxml msxml3 msxml6)
        set(objects_${msxml} ${arch}-lib-xmldom)
        set(includes_${msxml} guest/lib)
        set(libs_${msxml} ${MIME_LIB_${arch}}/ole32.lib ${MIME_LIB_${arch}}/oleaut32.lib ${MIME_LIB_${arch}}/advapi32.lib)
    endforeach()
    set(objects_advpack ${arch}-lib-cabinet)
    set(libs_advpack ${MIME_LIB_${arch}}/version.lib ${MIME_LIB_${arch}}/advapi32.lib)
    set(libs_msi ${MIME_LIB_${arch}}/advapi32.lib ${MIME_LIB_${arch}}/shell32.lib ${MIME_LIB_${arch}}/ole32.lib
        ${MIME_LIB_${arch}}/oleaut32.lib ${MIME_LIB_${arch}}/version.lib)
    set(objects_winmm ${arch}-lib-audio)
    set(objects_dsound ${arch}-lib-audio)
    set(objects_mmdevapi ${arch}-lib-audio)
    set(objects_dinput8 ${arch}-lib-dinput)
    set(objects_dinput ${arch}-lib-dinput)
    foreach(xinput xinput1_4 xinput1_3 xinput1_2 xinput1_1)
        set(objects_${xinput} ${arch}-lib-xinput)
        set(libs_${xinput} ${MIME_LIB_${arch}}/setupapi.lib)
    endforeach()
    set(objects_d2d1 ${arch}-lib-vector ${arch}-lib-dxbc:guest/lib/dxbc/container.c ${arch}-lib-icc ${arch}-lib-par)
    set(objects_mscms ${arch}-lib-icc)
    set(includes_mscms guest/lib)
    set(libs_mscms ${MIME_LIB_${arch}}/advapi32.lib)
    set(includes_d2d1 guest/lib ${MIME_GEN}/shaders)
    set(libs_d2d1 ${MIME_LIB_${arch}}/ole32.lib ${MIME_LIB_${arch}}/dwrite.lib)
    set(objects_gdiplus ${arch}-lib-vector ${arch}-lib-ttf)
    set(includes_gdiplus guest/lib)
    set(libs_gdiplus ${MIME_LIB_${arch}}/ole32.lib)
    set(libs_oledlg ${MIME_LIB_${arch}}/ole32.lib)
    set(libs_uianimation ${MIME_LIB_${arch}}/msvcrt.lib)
    set(libs_mlang ${MIME_LIB_${arch}}/oleaut32.lib)
    set(libs_secur32 ${MIME_LIB_${arch}}/advapi32.lib)
    set(libs_rsaenh ${MIME_LIB_${arch}}/advapi32.lib)
    set(objects_windowscodecs ${arch}-lib-png:guest/lib/png/inflate.c ${arch}-lib-png:guest/lib/png/deflate.c
        ${arch}-lib-pngio ${arch}-lib-jpeg ${arch}-lib-tiff ${arch}-lib-gif ${arch}-lib-bcn ${arch}-lib-icc ${arch}-lib-jxr ${arch}-lib-par)
    set(includes_windowscodecs guest/lib)
    set(libs_windowscodecs ${MIME_LIB_${arch}}/ole32.lib)
    set(objects_dwrite ${arch}-lib-ttf ${arch}-lib-png:guest/lib/png/inflate.c)
    set(includes_dwrite guest/lib)
    set(objects_bcrypt ${arch}-lib-crypto)
    set(objects_combase ${arch}-lib-crypto:guest/lib/crypto/hash.c)
    set(includes_combase guest/lib)
    set(objects_crypt32 ${arch}-lib-crypto ${arch}-lib-compound ${arch}-lib-cabinet)
    set(objects_secur32 ${arch}-lib-crypto)
    set(objects_advapi32 ${arch}-lib-crypto:guest/lib/crypto/hash.c ${arch}-lib-crypto:guest/lib/crypto/stream.c
        ${arch}-lib-crypto:guest/lib/crypto/bignum.c ${arch}-lib-crypto:guest/lib/crypto/rsa.c
        ${arch}-lib-crypto:guest/lib/crypto/aes.c ${arch}-lib-crypto:guest/lib/crypto/des.c
        ${arch}-lib-crypto:guest/lib/crypto/modes.c ${arch}-lib-crypto:guest/lib/crypto/kdf.c ${arch}-lib-crypto:guest/lib/crypto/hmac.c)
    foreach(crypto bcrypt crypt32 secur32 advapi32)
        set(includes_${crypto} guest/lib)
    endforeach()
    set(objects_riched20 ${arch}-lib-richedit)
    set(objects_msftedit ${arch}-lib-richedit)
    set(includes_riched20 guest/lib)
    set(includes_msftedit guest/lib)
    set(libs_riched32 ${MIME_LIB_${arch}}/riched20.lib)
    set(libs_oleaut32 ${MIME_LIB_${arch}}/ole32.lib ${MIME_LIB_${arch}}/rpcrt4.lib ${MIME_LIB_${arch}}/msvcrt.lib)
    set(objects_oleaut32 ${arch}-lib-jpeg)
    set(includes_oleaut32 guest/lib)
    set(libs_ole32 ${MIME_LIB_${arch}}/rpcrt4.lib)
    set(objects_winhttp ${arch}-lib-png:guest/lib/png/inflate.c)
    set(objects_winspool ${arch}-lib-png:guest/lib/png/deflate.c ${arch}-lib-png:guest/lib/png/inflate.c)
    set(includes_winspool guest/lib)
    set(includes_winhttp guest/lib)
    set(libs_winhttp ${MIME_LIB_${arch}}/ws2_32.lib)
    set(libs_wsock32 ${MIME_LIB_${arch}}/ws2_32.lib)
    set(libs_dnsapi ${MIME_LIB_${arch}}/ws2_32.lib)
    set(objects_wininet ${arch}-lib-png:guest/lib/png/inflate.c)
    set(includes_wininet guest/lib)
    set(libs_wininet ${MIME_LIB_${arch}}/ws2_32.lib ${MIME_LIB_${arch}}/kernelbase.lib)
    set(libs_urlmon ${MIME_LIB_${arch}}/wininet.lib ${MIME_LIB_${arch}}/ole32.lib ${MIME_LIB_${arch}}/oleaut32.lib)
    set(objects_dxgi ${arch}-lib-privdata)
    set(objects_d3d11 ${arch}-lib-privdata ${arch}-lib-dxbc)
    set(objects_d3d12 ${arch}-lib-privdata ${arch}-lib-dxbc ${arch}-lib-dxil ${arch}-d3d11:guest/d3d11/format.c ${arch}-d3d11:guest/d3d11/vkprofile.c ${arch}-d3d11:guest/d3d11/gfxshare.c)
    set(objects_d3dcompiler_47 ${arch}-lib-dxbc:guest/lib/dxbc/container.c ${arch}-lib-dxbc:guest/lib/dxbc/reader.c ${arch}-lib-hlsl)
    set(objects_d3d9 ${arch}-lib-privdata ${arch}-lib-dxbc:guest/lib/dxbc/container.c)
    set(objects_d3d10 ${arch}-lib-dxbc:guest/lib/dxbc/container.c)
    set(objects_d3d10_1 ${arch}-d3d10:guest/d3d10/create.c)
    set(objects_d3d10_1core ${arch}-d3d10core:guest/d3d10core/kmt.c)
    set(libs_wintrust ${MIME_LIB_${arch}}/crypt32.lib)
    set(libs_cryptnet ${MIME_LIB_${arch}}/crypt32.lib)
    set(libs_msvfw32 ${MIME_LIB_${arch}}/winmm.lib ${MIME_LIB_${arch}}/comdlg32.lib)
    set(libs_authz ${MIME_LIB_${arch}}/advapi32.lib)
    set(libs_hlink ${MIME_LIB_${arch}}/shell32.lib)
    set(libs_mapi32 ${MIME_LIB_${arch}}/ole32.lib)
    set(objects_wintrust ${arch}-lib-compound ${arch}-lib-crypto:guest/lib/crypto/hash.c)
    set(includes_wintrust guest/lib)
    set(libs_msacm32 ${MIME_LIB_${arch}}/winmm.lib)
    set(libs_setupapi ${MIME_LIB_${arch}}/cfgmgr32.lib ${MIME_LIB_${arch}}/advapi32.lib ${MIME_LIB_${arch}}/version.lib)
    set(objects_setupapi ${arch}-lib-cabinet)
    set(libs_shell32 ${MIME_LIB_${arch}}/kernelbase.lib ${MIME_LIB_${arch}}/ole32.lib ${MIME_LIB_${arch}}/oleaut32.lib
        ${MIME_LIB_${arch}}/shlwapi.lib)
    set(libs_onecorecommonproxystub ${MIME_LIB_${arch}}/ole32.lib ${MIME_LIB_${arch}}/rpcrt4.lib)
    set(libs_onecoreuapcommonproxystub ${MIME_LIB_${arch}}/ole32.lib ${MIME_LIB_${arch}}/rpcrt4.lib)
    set(libs_explorerframe ${MIME_LIB_${arch}}/ole32.lib ${MIME_LIB_${arch}}/oleaut32.lib ${MIME_LIB_${arch}}/shell32.lib)
    set(libs_ieframe ${MIME_LIB_${arch}}/ole32.lib ${MIME_LIB_${arch}}/oleaut32.lib ${MIME_LIB_${arch}}/advapi32.lib)
    set(objects_zipfldr ${arch}-lib-png:guest/lib/png/inflate.c)
    set(libs_avifil32 ${MIME_LIB_${arch}}/ole32.lib ${MIME_LIB_${arch}}/msacm32.lib)
    set(includes_zipfldr guest/lib)
    set(libs_zipfldr ${MIME_LIB_${arch}}/ole32.lib ${MIME_LIB_${arch}}/oleaut32.lib ${MIME_LIB_${arch}}/shell32.lib)
    set(libs_msado15 ${MIME_LIB_${arch}}/odbc32.lib ${MIME_LIB_${arch}}/ole32.lib ${MIME_LIB_${arch}}/oleaut32.lib ${MIME_LIB_${arch}}/advapi32.lib)
    set(libs_scrrun ${MIME_LIB_${arch}}/ole32.lib ${MIME_LIB_${arch}}/oleaut32.lib ${MIME_LIB_${arch}}/advapi32.lib
        ${MIME_LIB_${arch}}/version.lib ${MIME_LIB_${arch}}/mpr.lib ${MIME_LIB_${arch}}/shell32.lib)
    set(libs_wshom ${MIME_LIB_${arch}}/ole32.lib ${MIME_LIB_${arch}}/oleaut32.lib ${MIME_LIB_${arch}}/advapi32.lib
        ${MIME_LIB_${arch}}/shell32.lib ${MIME_LIB_${arch}}/mpr.lib ${MIME_LIB_${arch}}/winspool.lib
        ${MIME_LIB_${arch}}/scrrun.lib)
    set(libs_mstask ${MIME_LIB_${arch}}/ole32.lib ${MIME_LIB_${arch}}/advapi32.lib)
    set(libs_msdmo ${MIME_LIB_${arch}}/ole32.lib ${MIME_LIB_${arch}}/advapi32.lib)
    set(libs_vbscript ${MIME_LIB_${arch}}/ole32.lib ${MIME_LIB_${arch}}/oleaut32.lib ${MIME_LIB_${arch}}/advapi32.lib
        ${MIME_LIB_${arch}}/msvcrt.lib)
    set(libs_jscript ${MIME_LIB_${arch}}/ole32.lib ${MIME_LIB_${arch}}/oleaut32.lib ${MIME_LIB_${arch}}/advapi32.lib
        ${MIME_LIB_${arch}}/msvcrt.lib)
    set(libs_propsys ${MIME_LIB_${arch}}/ole32.lib ${MIME_LIB_${arch}}/oleaut32.lib ${MIME_LIB_${arch}}/advapi32.lib
        ${MIME_LIB_${arch}}/shlwapi.lib ${MIME_LIB_${arch}}/combase.lib)
    set(objects_propsys ${arch}-lib-crypto:guest/lib/crypto/hash.c)
    set(includes_propsys guest/lib)
    set(libs_shcore ${MIME_LIB_${arch}}/ole32.lib)
    set(libs_shlwapi ${MIME_LIB_${arch}}/shcore.lib)
    set(objects_shcore ${arch}-lib-winrt)
    set(includes_shcore guest/lib)
    set(objects_windows.graphics ${arch}-lib-winrt)
    set(includes_windows.graphics guest/lib)
    set(libs_windows.graphics ${MIME_LIB_${arch}}/ole32.lib ${MIME_LIB_${arch}}/combase.lib ${MIME_LIB_${arch}}/shcore.lib
        ${MIME_LIB_${arch}}/advapi32.lib)
    set(libs_comdlg32 ${MIME_LIB_${arch}}/comctl32.lib ${MIME_LIB_${arch}}/shell32.lib
        ${MIME_LIB_${arch}}/shlwapi.lib ${MIME_LIB_${arch}}/ole32.lib ${MIME_LIB_${arch}}/advapi32.lib)
    set(libs_windows_storage ${MIME_LIB_${arch}}/ole32.lib ${MIME_LIB_${arch}}/shell32.lib
        ${MIME_LIB_${arch}}/shlwapi.lib ${MIME_LIB_${arch}}/propsys.lib)
    set(libs_mfplat ${MIME_LIB_${arch}}/ole32.lib)
    set(libs_mfreadwrite ${MIME_LIB_${arch}}/mfplat.lib ${MIME_LIB_${arch}}/ole32.lib)
    set(libs_mf ${MIME_LIB_${arch}}/mfplat.lib ${MIME_LIB_${arch}}/cfgmgr32.lib ${MIME_LIB_${arch}}/ole32.lib)
    set(libs_mfcaptureengine ${MIME_LIB_${arch}}/mfplat.lib ${MIME_LIB_${arch}}/mf.lib ${MIME_LIB_${arch}}/ole32.lib)
    set(libs_comctl32 ${MIME_LIB_${arch}}/uxtheme.lib)
    set(libs_msctf ${MIME_LIB_${arch}}/ole32.lib ${MIME_LIB_${arch}}/oleaut32.lib)
    set(libs_oleacc ${MIME_LIB_${arch}}/ole32.lib ${MIME_LIB_${arch}}/oleaut32.lib)
    set(libs_uiautomationcore ${MIME_LIB_${arch}}/ole32.lib ${MIME_LIB_${arch}}/oleaut32.lib ${MIME_LIB_${arch}}/oleacc.lib
        ${MIME_LIB_${arch}}/advapi32.lib)
    foreach(service rpcss rpcepmap coremessaging)
        set(libs_${service} ${MIME_LIB_${arch}}/advapi32.lib)
    endforeach()
    list(APPEND libs_coremessaging ${MIME_LIB_${arch}}/ole32.lib ${MIME_LIB_${arch}}/combase.lib)
    set(libs_wkscli ${MIME_LIB_${arch}}/advapi32.lib ${MIME_LIB_${arch}}/rpcrt4.lib)
    set(libs_wkssvc ${MIME_LIB_${arch}}/advapi32.lib ${MIME_LIB_${arch}}/rpcrt4.lib)
    set(libs_nsisvc ${MIME_LIB_${arch}}/advapi32.lib)
    set(libs_keyiso ${MIME_LIB_${arch}}/advapi32.lib)
    set(libs_wbemcore ${MIME_LIB_${arch}}/advapi32.lib ${MIME_LIB_${arch}}/ole32.lib ${MIME_LIB_${arch}}/oleaut32.lib)
    set(libs_loadperf ${MIME_LIB_${arch}}/advapi32.lib)
    set(libs_bthprops ${MIME_LIB_${arch}}/rpcrt4.lib ${MIME_LIB_${arch}}/ole32.lib)
    set(libs_vssapi ${MIME_LIB_${arch}}/advapi32.lib ${MIME_LIB_${arch}}/ole32.lib)
    set(libs_activeds ${MIME_LIB_${arch}}/advapi32.lib ${MIME_LIB_${arch}}/netapi32.lib ${MIME_LIB_${arch}}/oleaut32.lib)
    set(includes_dxgi third_party/khronos)
    set(includes_d3d11 third_party/khronos ${MIME_GEN}/shaders)
    set(includes_d3d12 third_party/khronos third_party/SPIRV-Cross)
    set(includes_dcomp ${MIME_GEN}/shaders guest/lib)
    set(objects_dcomp ${arch}-lib-winrt:guest/lib/winrt/event.c)
    set(libs_dcomp ${MIME_LIB_${arch}}/combase.lib)
    foreach(entry IN LISTS MIME_EXTRA_DLLS)
        string(REPLACE ":" ";" entry ${entry})
        list(GET entry 0 name)
        list(GET entry 1 base)
        set(file ${name}.dll)
        if(name STREQUAL "winspool")
            set(file winspool.drv)
        elseif(name STREQUAL "bthprops")
            set(file bthprops.cpl)
        elseif(name STREQUAL "windows_storage")
            set(file windows.storage.dll)
        elseif(name STREQUAL "coremessaging")
            set(file CoreMessaging.dll)
        elseif(name STREQUAL "rpcepmap")
            set(file RpcEpMap.dll)
        elseif(name STREQUAL "wshom")
            set(file wshom.ocx)
        elseif(name STREQUAL "uianimation")
            set(file UIAnimation.dll)
        elseif(name STREQUAL "windows.graphics")
            set(file Windows.Graphics.dll)
        endif()
        mime_glob(sources guest/${name}/*.c guest/${name}/*.cpp guest/${name}/${arch}/*.S)
        mime_dll(${name} ${arch} BASE ${base} FILE ${file} SOURCES ${sources} OBJECTS ${arch}-chkstk ${objects_${name}}
            INCLUDES ${includes_${name}} IMPORTS ntdll kernel32 user32 gdi32 win32u LIBS ${libs_${name}}
            DEPENDS ${depends_${name}})
    endforeach()
endforeach()

mime_glob(services_sources guest/services/*.c)
set(services_arch ${MIME_GUEST_ARCH})
mime_objects(${services_arch}-services ${services_arch} SOURCES ${services_sources} INCLUDES guest/services guest/lib)
mime_link(${MIME_OUT_${services_arch}}/services.exe ${services_arch}
    OBJECTS ${services_arch}-services ${services_arch}-chkstk ${services_arch}-lib-crypto:guest/lib/crypto/hash.c
    LIBS ${MIME_LIB_${services_arch}}/ntdll.lib ${MIME_LIB_${services_arch}}/kernel32.lib
        ${MIME_LIB_${services_arch}}/advapi32.lib
    FLAGS /subsystem:console,10.0 /entry:entry /base:0x7FF6A8000000 /highentropyva /dynamicbase /stack:0x100000,0x4000
    RESOURCE services.exe ROM ${MIME_ROM_${services_arch}}/services.exe GROUPS guest)

mime_glob(svchost_sources guest/svchost/*.c)
foreach(arch IN LISTS MIME_USER_ARCHS ITEMS x86)
    if(arch STREQUAL "x86" OR arch STREQUAL MIME_GUEST_ARCH)
        mime_objects(${arch}-svchost ${arch} SOURCES ${svchost_sources} INCLUDES guest/svchost)
        set(svchost_libs ${MIME_LIB_${arch}}/ntdll.lib ${MIME_LIB_${arch}}/kernel32.lib
            ${MIME_LIB_${arch}}/advapi32.lib)
        set(svchost_flags /subsystem:console,10.0 /entry:entry /base:0x7FF6A9000000 /highentropyva /dynamicbase
            /stack:0x100000,0x4000)
        if(arch STREQUAL "x86")
            set(svchost_flags /subsystem:console,6.0 /entry:entry /base:0x00400000 /dynamicbase /stack:0x100000,0x4000)
        endif()
        mime_link(${MIME_OUT_${arch}}/svchost.exe ${arch} OBJECTS ${arch}-svchost ${arch}-chkstk LIBS ${svchost_libs}
            FLAGS ${svchost_flags} RESOURCE svchost.exe ROM ${MIME_ROM_${arch}}/svchost.exe GROUPS guest)
    endif()
endforeach()

mime_glob(msiexec_sources guest/msiexec/*.c)
foreach(arch IN LISTS MIME_USER_ARCHS ITEMS x86)
    if(arch STREQUAL "x86" OR arch STREQUAL MIME_GUEST_ARCH)
        mime_objects(${arch}-msiexec ${arch} SOURCES ${msiexec_sources} INCLUDES guest/msiexec)
        set(msiexec_libs ${MIME_LIB_${arch}}/ntdll.lib ${MIME_LIB_${arch}}/kernel32.lib ${MIME_LIB_${arch}}/user32.lib
            ${MIME_LIB_${arch}}/msi.lib)
        set(msiexec_flags /subsystem:windows,10.0 /entry:entry /base:0x7FF6AA000000 /highentropyva /dynamicbase
            /stack:0x100000,0x4000)
        if(arch STREQUAL "x86")
            set(msiexec_flags /subsystem:windows,6.0 /entry:entry /base:0x00400000 /dynamicbase /stack:0x100000,0x4000)
        endif()
        mime_link(${MIME_OUT_${arch}}/msiexec.exe ${arch} OBJECTS ${arch}-msiexec ${arch}-chkstk LIBS ${msiexec_libs}
            FLAGS ${msiexec_flags} RESOURCE msiexec.exe ROM ${MIME_ROM_${arch}}/msiexec.exe GROUPS guest)
    endif()
endforeach()

mime_glob(openwith_sources guest/openwith/*.c)
mime_objects(${MIME_GUEST_ARCH}-openwith ${MIME_GUEST_ARCH} SOURCES ${openwith_sources})
mime_link(${MIME_OUT_${MIME_GUEST_ARCH}}/openwith.exe ${MIME_GUEST_ARCH}
    OBJECTS ${MIME_GUEST_ARCH}-openwith ${MIME_GUEST_ARCH}-chkstk
    LIBS ${MIME_LIB_${MIME_GUEST_ARCH}}/ntdll.lib ${MIME_LIB_${MIME_GUEST_ARCH}}/kernel32.lib
        ${MIME_LIB_${MIME_GUEST_ARCH}}/advapi32.lib ${MIME_LIB_${MIME_GUEST_ARCH}}/shell32.lib
        ${MIME_LIB_${MIME_GUEST_ARCH}}/ole32.lib
    FLAGS /subsystem:console,10.0 /entry:entry /base:0x7FF6A7000000 /highentropyva /dynamicbase /stack:0x100000,0x4000
    RESOURCE openwith.exe ROM ${MIME_ROM_${MIME_GUEST_ARCH}}/openwith.exe GROUPS guest)

mime_glob(regsvr32_sources guest/regsvr32/*.c)
foreach(arch IN LISTS MIME_USER_ARCHS ITEMS x86)
    if(arch STREQUAL "x86" OR arch STREQUAL MIME_GUEST_ARCH)
        mime_objects(${arch}-regsvr32 ${arch} SOURCES ${regsvr32_sources} INCLUDES guest/regsvr32)
        set(regsvr32_libs ${MIME_LIB_${arch}}/ntdll.lib ${MIME_LIB_${arch}}/kernel32.lib ${MIME_LIB_${arch}}/user32.lib
            ${MIME_LIB_${arch}}/ole32.lib)
        set(regsvr32_flags /subsystem:windows,10.0 /entry:entry /base:0x7FF6AB000000 /highentropyva /dynamicbase
            /stack:0x100000,0x4000)
        if(arch STREQUAL "x86")
            set(regsvr32_flags /subsystem:windows,6.0 /entry:entry /base:0x00400000 /dynamicbase /stack:0x100000,0x4000)
        endif()
        mime_link(${MIME_OUT_${arch}}/regsvr32.exe ${arch} OBJECTS ${arch}-regsvr32 ${arch}-chkstk LIBS ${regsvr32_libs}
            FLAGS ${regsvr32_flags} RESOURCE regsvr32.exe ROM ${MIME_ROM_${arch}}/regsvr32.exe GROUPS guest)
    endif()
endforeach()

mime_glob(rundll32_sources guest/rundll32/*.c)
foreach(arch IN LISTS MIME_USER_ARCHS ITEMS x86)
    if(arch STREQUAL "x86" OR arch STREQUAL MIME_GUEST_ARCH)
        mime_objects(${arch}-rundll32 ${arch} SOURCES ${rundll32_sources} INCLUDES guest/rundll32)
        set(rundll32_libs ${MIME_LIB_${arch}}/ntdll.lib ${MIME_LIB_${arch}}/kernel32.lib ${MIME_LIB_${arch}}/user32.lib
            ${MIME_LIB_${arch}}/ole32.lib)
        set(rundll32_flags /subsystem:windows,10.0 /entry:entry /base:0x7FF6AC000000 /highentropyva /dynamicbase
            /stack:0x100000,0x4000)
        if(arch STREQUAL "x86")
            set(rundll32_flags /subsystem:windows,6.0 /entry:entry /base:0x00400000 /dynamicbase /stack:0x100000,0x4000)
        endif()
        mime_link(${MIME_OUT_${arch}}/rundll32.exe ${arch} OBJECTS ${arch}-rundll32 ${arch}-chkstk LIBS ${rundll32_libs}
            FLAGS ${rundll32_flags} RESOURCE rundll32.exe ROM ${MIME_ROM_${arch}}/rundll32.exe GROUPS guest)
    endif()
endforeach()

# the script hosts, one source tree built twice: cscript.exe for the console and wscript.exe for windows.
mime_glob(wscript_sources guest/wscript/*.c)
foreach(arch IN LISTS MIME_USER_ARCHS ITEMS x86)
    if(arch STREQUAL "x86" OR arch STREQUAL MIME_GUEST_ARCH)
        foreach(host IN ITEMS wscript cscript)
            if(host STREQUAL "cscript")
                set(host_base 0x7FF6A6000000)
                set(host_subsystem console)
                set(host_options -DWSH_CONSOLE=1)
            else()
                set(host_base 0x7FF6A5000000)
                set(host_subsystem windows)
                set(host_options -DWSH_CONSOLE=0)
            endif()
            mime_objects(${arch}-${host} ${arch} SOURCES ${wscript_sources} INCLUDES guest/wscript OPTIONS ${host_options})
            set(host_libs ${MIME_LIB_${arch}}/ntdll.lib ${MIME_LIB_${arch}}/kernel32.lib ${MIME_LIB_${arch}}/user32.lib
                ${MIME_LIB_${arch}}/advapi32.lib ${MIME_LIB_${arch}}/ole32.lib ${MIME_LIB_${arch}}/oleaut32.lib
                ${MIME_LIB_${arch}}/shell32.lib ${MIME_LIB_${arch}}/version.lib)
            set(host_flags /subsystem:${host_subsystem},10.0 /entry:entry /base:${host_base} /highentropyva /dynamicbase
                /stack:0x100000,0x4000)
            if(arch STREQUAL "x86")
                set(host_flags /subsystem:${host_subsystem},6.0 /entry:entry /base:0x00400000 /dynamicbase
                    /stack:0x100000,0x4000)
            endif()
            mime_arch_script(host_script ${CMAKE_SOURCE_DIR}/guest/wscript/${host} ${arch})
            mime_link(${MIME_OUT_${arch}}/${host}.exe ${arch} OBJECTS ${arch}-${host} ${arch}-chkstk LIBS ${host_libs}
                FLAGS ${host_flags} RESOURCE ${host}.exe RESOURCE_SCRIPT ${host_script}
                ROM ${MIME_ROM_${arch}}/${host}.exe GROUPS guest)
        endforeach()
    endif()
endforeach()

set(MIME_ACM_CODECS msg711:0x7FF360000000 imaadp32:0x7FF350000000 msadp32:0x7FF340000000 msgsm32:0x7FF330000000
    l3codeca:0x7FF3F0000000)
foreach(arch IN LISTS MIME_USER_ARCHS)
    foreach(entry IN LISTS MIME_ACM_CODECS)
        string(REPLACE ":" ";" entry ${entry})
        list(GET entry 0 name)
        list(GET entry 1 base)
        mime_glob(sources guest/${name}/*.c)
        mime_dll(${name} ${arch} BASE ${base} FILE ${name}.acm SOURCES ${sources} OBJECTS ${arch}-chkstk
            INCLUDES guest/lib IMPORTS ntdll kernel32 user32)
    endforeach()
endforeach()

set(MIME_MCI_DRIVERS mciwave:0x7FF280000000:winmm mciseq:0x7FF270000000:winmm
    mciqtz32:0x7FF260000000:winmm,msacm32 mcicda:0x7FF250000000:winmm)
foreach(arch IN LISTS MIME_USER_ARCHS)
    foreach(entry IN LISTS MIME_MCI_DRIVERS)
        string(REPLACE ":" ";" entry ${entry})
        list(GET entry 0 name)
        list(GET entry 1 base)
        list(GET entry 2 extra)
        string(REPLACE "," ";" extra ${extra})
        mime_glob(sources guest/${name}/*.c)
        mime_dll(${name} ${arch} BASE ${base} SOURCES ${sources} OBJECTS ${arch}-chkstk
            IMPORTS ntdll kernel32 user32 ${extra})
    endforeach()
endforeach()

mime_glob(MIME_MMDEVAPI_SOURCES guest/mmdevapi/*.c)
mime_glob(MIME_OPENGL32_SOURCES guest/opengl32/*.c)

set(opengl32_options -Wno-ignored-attributes -DNTSYSCALLAPI= -DW32KAPI=)
set(opengl32_includes guest/opengl32 third_party/khronos ${MIME_GEN}/opengl32)
if(MIME_GUEST_ARCH STREQUAL "arm64")
    mime_dll(opengl32 arm64 BASE 0x7FF550000000 ENTRY /noentry
        SOURCES ${MIME_OPENGL32_SOURCES} ${MIME_GEN}/opengl32/procs.c ${MIME_GEN}/opengl32/dlist_gen.c ${MIME_GEN}/opengl32/x64_thunks.c
        OBJECTS arm64-ntsyscalls arm64-win32ksyscalls ${MIME_RUNTIME_arm64} OPTIONS ${opengl32_options}
        INCLUDES ${opengl32_includes} DEF ${MIME_GEN}/opengl32/exports.def NOIMPLIB)
    mime_dll(opengl32 x64 BASE 0x7FF540000000
        SOURCES guest/opengl32/x64/main.c guest/opengl32/x64/native.S ${MIME_GEN}/opengl32/x64_stubs.S
        OBJECTS x64-chkstk OPTIONS ${opengl32_options} INCLUDES ${opengl32_includes} IMPORTS ntdll kernel32
        DEF ${MIME_GEN}/opengl32/x64.def NOIMPLIB)
else()
    mime_dll(opengl32 x64 BASE 0x7FF550000000 ENTRY /noentry
        SOURCES ${MIME_OPENGL32_SOURCES} ${MIME_GEN}/opengl32/procs.c ${MIME_GEN}/opengl32/dlist_gen.c
        OBJECTS x64-ntsyscalls x64-win32ksyscalls ${MIME_RUNTIME_x64} OPTIONS ${opengl32_options}
        INCLUDES ${opengl32_includes} DEF ${MIME_GEN}/opengl32/native.def NOIMPLIB)
endif()

# vulkan, forwarded over the wire to the host's driver: the system's on linux, mime's own on metal on macos.
mime_glob(MIME_VULKAN_SOURCES guest/vulkan-1/*.c)
set(MIME_VULKAN_GEN_SOURCES mvk_structs.c mvk_guest_commands.c mvk_guest_out.c mvk_guest_procs.c)
list(TRANSFORM MIME_VULKAN_GEN_SOURCES PREPEND ${MIME_GEN}/vulkan/)
set(MIME_VULKAN_OPTIONS -DMVK_GUEST=1 -Wno-unused-variable -Wno-unused-function -Wno-unused-but-set-variable)
set(MIME_VULKAN_INCLUDES third_party/khronos ${MIME_GEN}/vulkan)
foreach(arch IN LISTS MIME_USER_ARCHS)
    mime_dll(vulkan-1 ${arch} BASE 0x7FF710000000 SOURCES ${MIME_VULKAN_SOURCES} ${MIME_VULKAN_GEN_SOURCES}
        OBJECTS ${arch}-chkstk OPTIONS ${MIME_VULKAN_OPTIONS} INCLUDES ${MIME_VULKAN_INCLUDES}
        IMPORTS ntdll kernel32 win32u user32 DEF ${MIME_GEN}/vulkan/mvk_exports.def)
endforeach()

if(MIME_GUEST_ARCH STREQUAL "x64")

    mime_objects(x64-wow32 x64 SOURCES guest/xta64/wow32.c guest/xta64/wow32_query.c guest/xta64/wow32_win32k.c
        guest/xta64/wow32_callback.c
        OPTIONS -fno-math-errno INCLUDES guest/xta64)
    mime_sources(sources wow64 x64)
    mime_dll(wow64 x64 BASE 0x7FF420000000 SOURCES ${sources}
        OBJECTS x64-chkstk x64-wow32:guest/xta64/wow32.c x64-wow32:guest/xta64/wow32_query.c
        INCLUDES guest/xta64 IMPORTS ntdll)
    mime_sources(sources wow64win x64)
    mime_dll(wow64win x64 BASE 0x7FF410000000 SOURCES ${sources}
        OBJECTS x64-chkstk x64-wow32:guest/xta64/wow32_win32k.c x64-wow32:guest/xta64/wow32_callback.c
        INCLUDES guest/xta64 IMPORTS ntdll wow64)
    mime_sources(sources wow64cpu x64)
    mime_dll(wow64cpu x64 BASE 0x78000000 SOURCES ${sources} OBJECTS x64-chkstk IMPORTS ntdll wow64)
endif()

# ===================================== regedit =====================================

# regedit.exe and reg.exe, and the reader and writer of .reg files that both are built on.
# regedit is at C:\Windows\regedit.exe and, for the 32 bit one, C:\Windows\SysWOW64\regedit.exe; reg is in the system folders.
set(MIME_ROM_WINDOWS ${MIME_ROM}/winroot)

mime_glob(regfile_sources guest/lib/regfile/*.c)
mime_glob(regedit_sources guest/regedit/*.c)
mime_glob(reg_sources guest/reg/*.c)

foreach(arch IN LISTS MIME_USER_ARCHS ITEMS x86)
    if(arch STREQUAL "x86" OR arch STREQUAL MIME_GUEST_ARCH)
        set(regedit_rom ${MIME_ROM_${arch}})
        if(arch STREQUAL MIME_GUEST_ARCH)
            set(regedit_rom ${MIME_ROM_WINDOWS})
        endif()
        mime_objects(${arch}-regfile ${arch} SOURCES ${regfile_sources} INCLUDES guest/lib/regfile)

        mime_objects(${arch}-regedit ${arch} SOURCES ${regedit_sources} INCLUDES guest/regedit guest/lib/regfile)
        set(regedit_libs ${MIME_LIB_${arch}}/ntdll.lib ${MIME_LIB_${arch}}/kernel32.lib ${MIME_LIB_${arch}}/user32.lib
            ${MIME_LIB_${arch}}/gdi32.lib ${MIME_LIB_${arch}}/advapi32.lib ${MIME_LIB_${arch}}/comctl32.lib
            ${MIME_LIB_${arch}}/comdlg32.lib ${MIME_LIB_${arch}}/shell32.lib)
        set(regedit_flags /subsystem:windows,10.0 /entry:entry /base:0x7FF6C4000000 /highentropyva /dynamicbase
            /stack:0x100000,0x4000)
        if(arch STREQUAL "x86")
            set(regedit_flags /subsystem:windows,6.0 /entry:entry /base:0x00400000 /dynamicbase /stack:0x100000,0x4000)
        endif()
        mime_link(${MIME_OUT_${arch}}/regedit.exe ${arch} OBJECTS ${arch}-regedit ${arch}-regfile ${arch}-chkstk
            LIBS ${regedit_libs} FLAGS ${regedit_flags} RESOURCE regedit.exe
            RESOURCE_SCRIPT ${CMAKE_SOURCE_DIR}/guest/regedit/regedit.rc ROM ${regedit_rom}/regedit.exe GROUPS guest)

        mime_objects(${arch}-reg ${arch} SOURCES ${reg_sources} INCLUDES guest/reg guest/lib/regfile)
        set(reg_libs ${MIME_LIB_${arch}}/ntdll.lib ${MIME_LIB_${arch}}/kernel32.lib ${MIME_LIB_${arch}}/advapi32.lib)
        set(reg_flags /subsystem:console,10.0 /entry:entry /base:0x7FF6C5000000 /highentropyva /dynamicbase
            /stack:0x100000,0x4000)
        if(arch STREQUAL "x86")
            set(reg_flags /subsystem:console,6.0 /entry:entry /base:0x00400000 /dynamicbase /stack:0x100000,0x4000)
        endif()
        mime_link(${MIME_OUT_${arch}}/reg.exe ${arch} OBJECTS ${arch}-reg ${arch}-regfile ${arch}-chkstk LIBS ${reg_libs}
            FLAGS ${reg_flags} RESOURCE reg.exe RESOURCE_SCRIPT ${CMAKE_SOURCE_DIR}/guest/reg/reg.rc
            ROM ${MIME_ROM_${arch}}/reg.exe GROUPS guest)
    endif()
endforeach()

# ===================================== notepad =====================================

# notepad.exe, which the shell opens text files with, in the system folder of the machine and of x86.
foreach(arch ${MIME_GUEST_ARCH} x86)
    mime_objects(${arch}-notepad ${arch} SOURCES guest/notepad/main.c)
    set(notepad_libs ${MIME_LIB_${arch}}/ntdll.lib ${MIME_LIB_${arch}}/kernel32.lib ${MIME_LIB_${arch}}/user32.lib
        ${MIME_LIB_${arch}}/gdi32.lib ${MIME_LIB_${arch}}/comdlg32.lib ${MIME_LIB_${arch}}/shell32.lib)
    set(notepad_flags /subsystem:windows,10.0 /entry:entry /base:0x7FF6C6000000 /highentropyva /dynamicbase
        /stack:0x100000,0x4000)
    if(arch STREQUAL "x86")
        set(notepad_flags /subsystem:windows,6.0 /entry:entry /base:0x00400000 /dynamicbase /stack:0x100000,0x4000)
    endif()
    mime_link(${MIME_OUT_${arch}}/notepad.exe ${arch} OBJECTS ${arch}-notepad ${arch}-chkstk LIBS ${notepad_libs}
        FLAGS ${notepad_flags} RESOURCE notepad.exe RESOURCE_SCRIPT ${CMAKE_SOURCE_DIR}/guest/notepad/notepad.rc
        ROM ${MIME_ROM_${arch}}/notepad.exe GROUPS guest)
endforeach()

# ===================================== x86 =====================================

set(gen86 ${MIME_GEN}/x86)
set(stubs86 ${gen86})
if(MIME_GUEST_ARCH STREQUAL "x64")
    set(stubs86 ${gen86}/wow64)
endif()

mime_objects(x86-ntsyscalls x86 SOURCES ${stubs86}/ntdll_syscalls.S)
mime_glob(sources guest/ntdll/*.c guest/ntdll/x86/*.c guest/ntdll/x86/*.S)
mime_objects(x86-ntdll x86 SOURCES ${sources} guest/lib/crypto/hash.c INCLUDES guest/ntdll guest/lib)
set(objects x86-ntdll x86-ntsyscalls ${MIME_RUNTIME_x86})
mime_dll(ntdll x86 BASE 0x77000000 ENTRY /noentry OBJECTS ${objects} DEF ${gen86}/ntdll.def
    IMPORTS_DEF ${gen86}/ntdll.imports.def)

mime_sources(sources kernelbase x86)
mime_dll(kernelbase x86 BASE 0x76000000 SOURCES ${sources} OBJECTS x86-chkstk OPTIONS -DMIME_BUILD_KERNELBASE
    IMPORTS ntdll)

mime_objects(x86-kernel32-thunks x86 SOURCES ${gen86}/kernel32_thunks.S)
mime_dll(kernel32 x86 BASE 0x75000000 SOURCES guest/kernel32/main.c guest/kernel32/transacted.c guest/kernel32/binary.c
    OBJECTS x86-kernel32-thunks
    OPTIONS -DMIME_BUILD_KERNEL32 IMPORTS kernelbase ntdll DEF ${gen86}/kernel32.def
    IMPORTS_DEF ${gen86}/kernel32.imports.def)

mime_dll(mscoree x86 BASE 0x03000000 SOURCES guest/mscoree/main.c guest/kernelbase/features.c guest/mscoree/x86/common.S ${gen86}/mscoree_thunks.S OBJECTS x86-chkstk IMPORTS ntdll kernel32)

set(sources ${stubs86}/win32u_syscalls.S)
set(imports)
if(MIME_GUEST_ARCH STREQUAL "x64")
    list(APPEND sources guest/win32u/x86/wow64.S)
    set(imports ntdll)
endif()
mime_dll(win32u x86 BASE 0x74000000 ENTRY /noentry SOURCES ${sources} IMPORTS ${imports})

mime_sources(sources gdi32 x86)
mime_dll(gdi32 x86 BASE 0x73000000 SOURCES ${sources} guest/kernelbase/features.c OBJECTS x86-chkstk OPTIONS -DMIME_BUILD_GDI32
    IMPORTS ntdll kernel32 win32u)
mime_sources(sources user32 x86)
mime_dll(user32 x86 BASE 0x72000000 SOURCES ${sources} OBJECTS x86-chkstk x86-lib-png
    OPTIONS -DMIME_BUILD_USER32 INCLUDES guest/lib IMPORTS ntdll kernel32 win32u gdi32 FLAGS ${MIME_USER32_ORDINALS_x86})
mime_dll(imageres x86 BASE 0x6C000000 ENTRY /noentry NOIMPLIB DEF ${CMAKE_SOURCE_DIR}/guest/imageres/imageres.def)

set(all_five ntdll kernel32 user32 gdi32 win32u)
set(MIME_X86_DLLS
    shlwapi:0x71000000:all_five shell32:0x70000000:shell32_imports winmm:0x6f000000:all_five version:0x6e000000:all_five
    advapi32:0x6d000000:all_five sechost:0x0F000000:base ws2_32:0x6c000000:all_five uxtheme:0x6b000000:all_five
    netapi32:0x6a000000:all_five wtsapi32:0x69000000:all_five secur32:0x68000000:secur32_imports
    crypt32:0x67000000:all_five userenv:0x66000000:all_five ole32:0x65000000:ole32_imports oleaut32:0x64000000:oleaut32_imports
    rpcrt4:0x63000000:rpcrt4_imports imm32:0x62000000:all_five comctl32:0x61000000:comctl comdlg32:0x60000000:dialog_imports
    msi:0x5f000000:msi_imports mpr:0x5e000000:base wintrust:0x5d000000:crypt odbc32:0x5c000000:user
    gdiplus:0x5b000000:gdiplus_imports d2d1:0x1E500000:d2d1_imports msvbvm60:0x1E400000:msvbvm60_imports wininet:0x5a000000:wininet_imports urlmon:0x59000000:urlmon_imports msimg32:0x58000000:gdi
    oleacc:0x57000000:oleacc_imports oledlg:0x56000000:oledlg_imports combase:0x54000000:all_five usp10:0x51000000:gdi
    cfgmgr32:0x39000000:base setupapi:0x46000000:setup dwrite:0x45000000:all_five msacm32:0x42000000:acm dsound:0x3D000000:all_five ddraw:0x3C000000:direct3d
    dinput8:0x3B000000:all_five dinput:0x3A000000:all_five hid:0x38000000:base xinput1_4:0x37000000:xinput
    xinput1_3:0x36000000:xinput xinput1_2:0x35000000:xinput xinput1_1:0x34000000:xinput xinput9_1_0:0x33000000:base
    winhttp:0x2C000000:http windows_storage:0x15000000:storage_imports explorerframe:0x14000000:explorerframe_imports ieframe:0x55700000:ieframe_imports
    dwmapi:0x13000000:all_five prntvpt:0x11000000:all_five psapi:0x0D000000:base lz32:0x0C000000:base wsock32:0x0B000000:http mswsock:0x0A000000:base
    propsys:0x2B000000:propsys_imports shcore:0x2A000000:shcore_imports powrprof:0x29000000:all_five
    bcryptprimitives:0x28000000:all_five pdh:0x27000000:all_five wevtapi:0x26000000:all_five tbs:0x25000000:all_five
    ncrypt:0x24000000:all_five credui:0x23000000:all_five cryptui:0x22000000:all_five ndfapi:0x21000000:all_five
    winusb:0x20000000:all_five dhcpcsvc:0x1E000000:all_five esent:0x1D000000:all_five
    uiautomationcore:0x1C000000:uia_imports iphlpapi:0x1B000000:base bcrypt:0x1A000000:all_five
    mfplat:0x12400000:mfplat_imports mf:0x12200000:mf_imports mfreadwrite:0x12000000:mfreadwrite_imports
    mfcaptureengine:0x12100000:mfce_imports
    schannel:0x19000000:base msctf:0x0E000000:ctf_imports activeds:0x47000000:ads_imports advpack:0x10000000:advpack_imports
    imagehlp:0x02000000:base cabinet:0x09000000:base sfc:0x08000000:base sfc_os:0x07000000:sfc_os_imports
    dnsapi:0x04000000:dnsapi_imports msxml3:0x06000000:msxml_imports msxml6:0x05000000:msxml_imports windowscodecs:0x5A000000:wic_imports
    onecorecommonproxystub:0x02000000:proxy_imports onecoreuapcommonproxystub:0x01000000:proxy_imports
    feclient:0x03000000:base wkscli:0x55600000:wkscli_imports zipfldr:0x15800000:explorerframe_imports mstask:0x31D00000:mstask_imports msado15:0x31C00000:msado15_imports vbscript:0x30900000:vbscript_imports jscript:0x30A00000:jscript_imports scrrun:0x31B00000:scrrun_imports wshom:0x31A00000:wshom_imports avifil32:0x16800000:avifil32_imports glu32:0x16C00000:base dbghelp:0x32D00000:base odbccp32:0x55500000:base mscms:0x55400000:mscms_imports mapi32:0x55300000:mapi32_imports hlink:0x55200000:hlink_imports atl:0x55800000:base authz:0x55100000:authz_imports wldap32:0x55000000:base ntdsapi:0x32C00000:base mlang:0x32E00000:mlang_imports query:0x32B00000:all_five fontsub:0x32A00000:base tdh:0x32900000:base windows.networking:0x32800000:base dbgeng:0x32700000:base wmvcore:0x32600000:base atmlib:0x32500000:base avicap32:0x32400000:all_five rstrtmgr:0x32300000:base wlanapi:0x32200000:base snmpapi:0x32100000:base ktmw32:0x32000000:base httpapi:0x31F00000:base wbemcore:0x31E00000:wbem_imports loadperf:0x1E800000:loadperf_imports xmllite:0x1E700000:xmllite_imports security:0x1E600000:base sspicli:0x1E300000:base rsaenh:0x1E200000:mscms_imports msvfw32:0x1FC00000:msvfw32_imports normaliz:0x1F800000:base cryptsp:0x1F400000:base shfolder:0x1F000000:base msdmo:0x32F00000:mstask_imports dxva2:0x32F80000:user srvcli:0x1E900000:base netutils:0x1EA00000:base)
set(base ntdll kernel32)
set(mscms_imports ntdll kernel32 advapi32)
set(wbem_imports ntdll kernel32 advapi32 ole32 oleaut32)
set(loadperf_imports ntdll kernel32 advapi32)
set(xmllite_imports ntdll kernel32)
set(wkscli_imports ntdll kernel32 advapi32 rpcrt4 msvcrt)
set(dnsapi_imports ntdll kernel32 ws2_32)
set(oledlg_imports ntdll kernel32 user32 ole32)
set(msvfw32_imports ntdll kernel32 user32 gdi32 win32u winmm comdlg32)
set(authz_imports ntdll kernel32 advapi32)
set(mapi32_imports ntdll kernel32 ole32)
set(hlink_imports ntdll kernel32 user32 gdi32 win32u shell32)
set(msado15_imports ntdll kernel32 advapi32 ole32 oleaut32 odbc32)
set(scrrun_imports ntdll kernel32 user32 advapi32 ole32 oleaut32 version mpr shell32)
set(wshom_imports ntdll kernel32 user32 advapi32 ole32 oleaut32 shell32 mpr winspool scrrun)
set(vbscript_imports ntdll kernel32 user32 gdi32 win32u advapi32 ole32 oleaut32 msvcrt)
set(objects_vbscript x86-div)
set(jscript_imports ntdll kernel32 user32 advapi32 ole32 oleaut32 msvcrt)
set(objects_jscript x86-div)
set(mstask_imports ntdll kernel32 advapi32 ole32)
set(objects_mstask x86-div)
set(objects_zipfldr x86-lib-png:guest/lib/png/inflate.c x86-div)
set(avifil32_imports ntdll kernel32 user32 gdi32 win32u ole32 msacm32)
set(objects_avifil32 x86-div)
set(includes_zipfldr guest/lib)
set(rpcrt4_imports ntdll kernel32 user32 gdi32 win32u msvcrt)
set(ole32_imports ntdll kernel32 user32 gdi32 win32u rpcrt4 msvcrt)
set(oleaut32_imports ntdll kernel32 user32 gdi32 win32u ole32 rpcrt4 msvcrt)
set(propsys_imports ntdll kernel32 user32 gdi32 win32u ole32 oleaut32 advapi32 shlwapi combase)
set(mlang_imports ntdll kernel32 oleaut32)
set(secur32_imports ntdll kernel32 user32 gdi32 win32u advapi32)
set(objects_propsys x86-lib-crypto:guest/lib/crypto/hash.c)
set(includes_propsys guest/lib)
set(wic_imports ntdll kernel32 user32 gdi32 win32u ole32)
set(shcore_imports ntdll kernel32 user32 gdi32 win32u ole32)
set(objects_shcore x86-lib-winrt)
set(includes_shcore guest/lib)
set(proxy_imports ntdll kernel32 user32 gdi32 win32u ole32 rpcrt4)
set(shell32_imports ntdll kernel32 user32 gdi32 win32u ole32 oleaut32 shlwapi)
set(explorerframe_imports ntdll kernel32 user32 gdi32 win32u ole32 oleaut32 shell32)
set(ieframe_imports ntdll kernel32 user32 gdi32 win32u ole32 oleaut32 advapi32)
set(dialog_imports ntdll kernel32 user32 gdi32 win32u comctl32 shell32 shlwapi ole32 advapi32)
set(storage_imports ntdll kernel32 user32 gdi32 win32u ole32 shell32 shlwapi propsys)
set(comctl ntdll kernel32 user32 gdi32 win32u uxtheme)
set(mfplat_imports ntdll kernel32 ole32)
set(mfreadwrite_imports ntdll kernel32 mfplat ole32)
set(mf_imports ntdll kernel32 mfplat cfgmgr32 ole32)
set(mfce_imports ntdll kernel32 mfplat mf ole32)
set(user ntdll kernel32 user32)
set(ads_imports ntdll kernel32 advapi32 netapi32 oleaut32)
set(advpack_imports ntdll kernel32 advapi32 version)
set(objects_advpack x86-lib-cabinet)
set(objects_cabinet x86-lib-cabinet)
set(objects_wintrust x86-lib-compound x86-lib-crypto:guest/lib/crypto/hash.c)
set(includes_wintrust guest/lib)
set(msxml_imports ntdll kernel32 ole32 oleaut32 advapi32)
foreach(msxml msxml3 msxml6)
    set(objects_${msxml} x86-lib-xmldom)
    set(includes_${msxml} guest/lib)
endforeach()
set(sfc_os_imports ntdll kernel32 advapi32 version)
set(msi_imports ntdll kernel32 user32 gdi32 win32u advapi32 shell32 ole32 oleaut32 version)
set(oleacc_imports ntdll kernel32 user32 ole32 oleaut32)
set(uia_imports ntdll kernel32 user32 gdi32 win32u advapi32 ole32 oleaut32 oleacc)
set(ctf_imports ntdll kernel32 user32 gdi32 win32u ole32 oleaut32)
set(gdi ntdll kernel32 gdi32)
set(user_gdi ntdll kernel32 user32 gdi32)
set(gdiplus_imports ntdll kernel32 user32 gdi32 ole32)
set(d2d1_imports ntdll kernel32 user32 gdi32 ole32 dwrite)
set(msvbvm60_imports ntdll kernel32 user32 gdi32 ole32 oleaut32)
set(crypt ntdll kernel32 crypt32)
set(acm ntdll kernel32 user32 gdi32 winmm)
set(direct3d ntdll kernel32 user32 gdi32 win32u msvcrt)
set(setup ntdll kernel32 cfgmgr32 advapi32 version)
set(objects_setupapi x86-lib-cabinet)
set(xinput ntdll kernel32 user32 setupapi)
set(http ntdll kernel32 user32 ws2_32)
set(wininet_imports ntdll kernel32 user32 ws2_32 kernelbase)
set(urlmon_imports ntdll kernel32 user32 wininet ole32 oleaut32)
set(objects_oleaut32 x86-div x86-lib-jpeg)
set(includes_oleaut32 guest/lib)
set(objects_msi x86-lib-compound x86-lib-cabinet x86-lib-crypto:guest/lib/crypto/hash.c)
set(includes_msi guest/lib)
set(objects_winmm x86-lib-audio)
set(objects_dsound x86-lib-audio)
set(objects_dinput8 x86-lib-dinput)
set(objects_dinput x86-lib-dinput)
foreach(xinput xinput1_4 xinput1_3 xinput1_2 xinput1_1)
    set(objects_${xinput} x86-lib-xinput)
endforeach()
set(objects_gdiplus x86-lib-vector x86-lib-ttf x86-div)
set(includes_gdiplus guest/lib)
set(objects_msvbvm60 x86-div)
set(objects_d2d1 x86-lib-vector x86-div x86-lib-dxbc:guest/lib/dxbc/container.c x86-lib-icc x86-lib-par)
set(objects_mscms x86-lib-icc x86-div)
set(includes_mscms guest/lib)
set(includes_d2d1 guest/lib ${MIME_GEN}/shaders)
set(objects_dwrite x86-lib-ttf x86-lib-png:guest/lib/png/inflate.c x86-div)
set(includes_dwrite guest/lib)
set(objects_windowscodecs x86-lib-png:guest/lib/png/inflate.c x86-lib-png:guest/lib/png/deflate.c x86-lib-pngio x86-lib-jpeg x86-lib-tiff x86-lib-gif x86-lib-bcn x86-lib-icc x86-lib-jxr x86-lib-par x86-div)
set(includes_windowscodecs guest/lib)
set(objects_bcrypt x86-lib-crypto)
set(objects_combase x86-lib-crypto:guest/lib/crypto/hash.c)
set(includes_combase guest/lib)
set(objects_crypt32 x86-lib-crypto x86-lib-compound x86-lib-cabinet)
set(objects_secur32 x86-lib-crypto)
set(objects_advapi32 x86-lib-crypto:guest/lib/crypto/hash.c x86-lib-crypto:guest/lib/crypto/stream.c
        x86-lib-crypto:guest/lib/crypto/bignum.c x86-lib-crypto:guest/lib/crypto/rsa.c
        x86-lib-crypto:guest/lib/crypto/aes.c x86-lib-crypto:guest/lib/crypto/des.c x86-lib-crypto:guest/lib/crypto/modes.c x86-lib-crypto:guest/lib/crypto/kdf.c x86-lib-crypto:guest/lib/crypto/hmac.c)
foreach(crypto bcrypt crypt32 secur32 advapi32)
    set(includes_${crypto} guest/lib)
endforeach()
set(objects_winhttp x86-lib-png:guest/lib/png/inflate.c)
set(includes_winhttp guest/lib)
set(objects_wininet x86-lib-png:guest/lib/png/inflate.c)
set(includes_wininet guest/lib)

# the hlsl compiler catches its own faults through msvcrt's _except_handler3.
set(d3dcompiler_imports ${all_five} msvcrt)
list(APPEND MIME_X86_DLLS d3dcompiler_47:0x4F000000:d3dcompiler_imports)
list(APPEND MIME_X86_DLLS wldp:0x31900000:base cryptnet:0x55900000:crypt)
set(objects_d3dcompiler_47 x86-lib-dxbc:guest/lib/dxbc/container.c x86-lib-dxbc:guest/lib/dxbc/reader.c x86-lib-hlsl x86-div)

mime_dll(vulkan-1 x86 BASE 0x53000000 SOURCES ${MIME_VULKAN_SOURCES} ${MIME_VULKAN_GEN_SOURCES}
    OBJECTS x86-chkstk OPTIONS ${MIME_VULKAN_OPTIONS} INCLUDES ${MIME_VULKAN_INCLUDES}
    IMPORTS ntdll kernel32 win32u user32)
list(APPEND MIME_X86_DLLS dxgi:0x52000000:all_five d3d11:0x50000000:all_five
    d3d10core:0x4E000000:base d3d10_1core:0x4D000000:base d3d10:0x4C000000:all_five d3d10_1:0x4B000000:all_five
    d3d12:0x4A000000:all_five d3d9:0x49000000:all_five dcomp:0x13000000:all_five)
set(objects_dxgi x86-lib-privdata)
set(objects_d3d11 x86-lib-privdata x86-lib-dxbc)
set(objects_d3d10 x86-lib-dxbc:guest/lib/dxbc/container.c)
set(objects_d3d10_1 x86-d3d10:guest/d3d10/create.c)
set(objects_d3d10_1core x86-d3d10core:guest/d3d10core/kmt.c)
set(objects_d3d12 x86-lib-privdata x86-lib-dxbc x86-lib-dxil x86-d3d11:guest/d3d11/format.c x86-d3d11:guest/d3d11/vkprofile.c x86-d3d11:guest/d3d11/gfxshare.c)
set(objects_d3d9 x86-lib-privdata x86-lib-dxbc:guest/lib/dxbc/container.c)
set(includes_dxgi third_party/khronos)
set(includes_d3d11 third_party/khronos ${MIME_GEN}/shaders)
set(includes_d3d12 third_party/khronos third_party/SPIRV-Cross)
set(includes_dcomp ${MIME_GEN}/shaders)
# Windows.UI.Composition is served to 64 bit programs only.
set(objects_dcomp)
set(libs_dcomp)

foreach(entry IN LISTS MIME_X86_DLLS)
    string(REPLACE ":" ";" entry ${entry})
    list(GET entry 0 name)
    list(GET entry 1 address)
    list(GET entry 2 imports)
    mime_sources(sources ${name} x86)
    set(file ${name}.dll)
    if(name STREQUAL "windows_storage")
        set(file windows.storage.dll)
    elseif(name STREQUAL "wshom")
        set(file wshom.ocx)
    endif()
    mime_dll(${name} x86 BASE ${address} FILE ${file} SOURCES ${sources} OBJECTS x86-chkstk ${objects_${name}}
        INCLUDES ${includes_${name}} IMPORTS ${${imports}} DEPENDS ${depends_${name}})
endforeach()

mime_sources(sources d3d8 x86)
mime_dll(d3d8 x86 BASE 0x32000000 SOURCES ${sources} OBJECTS x86-chkstk x86-lib-privdata
    DEF ${CMAKE_SOURCE_DIR}/guest/d3d8/d3d8.def IMPORTS_DEF ${CMAKE_SOURCE_DIR}/guest/d3d8/d3d8.imports.def
    IMPORTS ntdll kernel32 user32 gdi32 d3d9)

foreach(entry riched20:0x18000000 msftedit:0x17000000)
    string(REPLACE ":" ";" entry ${entry})
    list(GET entry 0 name)
    list(GET entry 1 base)
    mime_sources(sources ${name} x86)
    mime_dll(${name} x86 BASE ${base} SOURCES ${sources} OBJECTS x86-chkstk x86-lib-richedit INCLUDES guest/lib
        DEF ${CMAKE_SOURCE_DIR}/guest/${name}/x86/${name}.def
        IMPORTS_DEF ${CMAKE_SOURCE_DIR}/guest/${name}/x86/${name}.imports.def IMPORTS ntdll kernel32 user32 gdi32 win32u)
endforeach()
mime_sources(sources riched32 x86)
mime_dll(riched32 x86 BASE 0x16000000 SOURCES ${sources} OBJECTS x86-chkstk IMPORTS ntdll kernel32 user32 riched20)

mime_glob(sources guest/winspool/*.c)
mime_dll(winspool x86 BASE 0x55000000 FILE winspool.drv SOURCES ${sources}
    OBJECTS x86-chkstk x86-lib-png:guest/lib/png/deflate.c x86-lib-png:guest/lib/png/inflate.c INCLUDES guest/lib
    IMPORTS ntdll kernel32 user32 gdi32)

mime_sources(sources bthprops x86)
mime_dll(bthprops x86 BASE 0x1F000000 FILE bthprops.cpl SOURCES ${sources} OBJECTS x86-chkstk IMPORTS ${all_five} rpcrt4 ole32)

mime_sources(sources msvcrt x86)
mime_objects(x86-msvcrt x86 SOURCES ${sources} INCLUDES guest/msvcrt)
mime_dll(msvcrt x86 BASE 0x48000000 OBJECTS x86-msvcrt x86-chkstk x86-div IMPORTS ntdll kernel32
    DEF ${gen86}/msvcrt.def IMPORTS_DEF ${gen86}/msvcrt.imports.def)

mime_objects(x86-ucrtbase x86 SOURCES guest/ucrtbase/main.c INCLUDES guest/ucrtbase)
mime_dll(ucrtbase x86 BASE 0x47000000 ENTRY /noentry OBJECTS x86-ucrtbase x86-chkstk DEF ${gen86}/ucrtbase.def
    IMPORTS_DEF ${gen86}/ucrtbase.imports.def DEPENDS ${MIME_LIB_x86}/msvcrt.lib)

mime_dll(opengl32 x86 BASE 0x44000000 ENTRY /noentry SOURCES ${MIME_OPENGL32_SOURCES} ${MIME_GEN}/opengl32/procs.c ${MIME_GEN}/opengl32/dlist_gen.c
    OBJECTS ${MIME_RUNTIME_x86} OPTIONS ${opengl32_options} INCLUDES ${opengl32_includes} IMPORTS ntdll win32u)

mime_dll(mmdevapi x86 BASE 0x45000000 SOURCES ${MIME_MMDEVAPI_SOURCES} OBJECTS x86-chkstk x86-lib-audio
    IMPORTS ntdll kernel32 user32 gdi32 win32u)
mime_dll(avrt x86 BASE 0x43000000 SOURCES guest/avrt/main.c OBJECTS x86-chkstk IMPORTS ntdll kernel32 user32 gdi32 win32u)

set(MIME_X86_MCI_DRIVERS mciwave:0x30000000:winmm mciseq:0x2F000000:winmm mciqtz32:0x2E000000:winmm,msacm32
    mcicda:0x2D000000:winmm)
foreach(entry IN LISTS MIME_X86_MCI_DRIVERS)
    string(REPLACE ":" ";" entry ${entry})
    list(GET entry 0 name)
    list(GET entry 1 base)
    list(GET entry 2 extra)
    string(REPLACE "," ";" extra ${extra})
    mime_sources(sources ${name} x86)
    mime_dll(${name} x86 BASE ${base} SOURCES ${sources} OBJECTS x86-chkstk IMPORTS ntdll kernel32 user32 ${extra})
endforeach()

set(MIME_X86_ACM_CODECS msg711:0x41000000 imaadp32:0x40000000 msadp32:0x3F000000 msgsm32:0x3E000000
    l3codeca:0x31000000)
foreach(entry IN LISTS MIME_X86_ACM_CODECS)
    string(REPLACE ":" ";" entry ${entry})
    list(GET entry 0 name)
    list(GET entry 1 base)
    mime_sources(sources ${name} x86)
    mime_dll(${name} x86 BASE ${base} FILE ${name}.acm SOURCES ${sources} OBJECTS x86-chkstk INCLUDES guest/lib
        IMPORTS ntdll kernel32 user32)
endforeach()

# ===================================== tools =====================================

# the console tools of System32 and SysWOW64: sc, taskkill, tasklist and the like. each tool is guest/<tool>/*.c and
# shares the library of guest/toolutil, which owns the entry point and the output.

# the libraries each tool imports besides ntdll and kernel32.
set(MIME_TOOL_LIBS_sc advapi32)
set(MIME_TOOL_LIBS_taskkill advapi32 user32)
set(MIME_TOOL_LIBS_tasklist advapi32 user32)
set(MIME_TOOL_LIBS_whoami advapi32)
set(MIME_TOOL_LIBS_setx advapi32 user32)
set(MIME_TOOL_LIBS_net advapi32)
set(MIME_TOOL_LIBS_fsutil advapi32)
set(MIME_TOOL_LIBS_netsh advapi32)
set(MIME_TOOL_LIBS_lodctr advapi32)
set(MIME_TOOL_LIBS_unlodctr advapi32)
set(MIME_TOOL_LIBS_mofcomp ole32 oleaut32)

# the tools that live in the wbem folder of System32 and SysWOW64.
set(MIME_TOOLS_WBEM mofcomp)

# the objects a tool links besides toolutil.
set(MIME_TOOL_OBJECTS_taskkill procutil)
set(MIME_TOOL_OBJECTS_tasklist procutil)

set(MIME_TOOLS sc taskkill tasklist timeout choice find findstr where whoami hostname setx attrib xcopy net fsutil netsh lodctr unlodctr mofcomp)

set(tool_index 0)
foreach(arch IN LISTS MIME_USER_ARCHS ITEMS x86)
    if(NOT arch STREQUAL "x86" AND NOT arch STREQUAL MIME_GUEST_ARCH)
        continue()
    endif()
    mime_objects(${arch}-toolutil ${arch} SOURCES guest/toolutil/tool.c guest/toolutil/table.c INCLUDES guest/toolutil)
    mime_objects(${arch}-procutil ${arch} SOURCES guest/procutil/proc.c INCLUDES guest/toolutil guest/procutil)
endforeach()
foreach(tool IN LISTS MIME_TOOLS)
    if(NOT EXISTS ${CMAKE_SOURCE_DIR}/guest/${tool})
        continue()
    endif()
    mime_glob(tool_sources guest/${tool}/*.c)
    math(EXPR tool_index "${tool_index} + 1")
    foreach(arch IN LISTS MIME_USER_ARCHS ITEMS x86)
        if(NOT arch STREQUAL "x86" AND NOT arch STREQUAL MIME_GUEST_ARCH)
            continue()
        endif()
        mime_objects(${arch}-${tool} ${arch} SOURCES ${tool_sources} INCLUDES guest/toolutil guest/procutil guest/${tool})
        set(tool_libs ${MIME_LIB_${arch}}/ntdll.lib ${MIME_LIB_${arch}}/kernel32.lib)
        foreach(lib IN LISTS MIME_TOOL_LIBS_${tool})
            list(APPEND tool_libs ${MIME_LIB_${arch}}/${lib}.lib)
        endforeach()
        set(tool_objects ${arch}-${tool} ${arch}-toolutil ${arch}-chkstk)
        foreach(extra IN LISTS MIME_TOOL_OBJECTS_${tool})
            list(APPEND tool_objects ${arch}-${extra})
        endforeach()
        if(arch STREQUAL "x86")
            list(APPEND tool_objects x86-div)
            set(tool_flags /subsystem:console,6.0 /entry:entry /base:0x00400000 /dynamicbase /stack:0x100000,0x4000)
        else()
            math(EXPR tool_base "0x7FF6B0000000 + ${tool_index} * 0x1000000" OUTPUT_FORMAT HEXADECIMAL)
            set(tool_flags /subsystem:console,10.0 /entry:entry /base:${tool_base} /highentropyva /dynamicbase
                /stack:0x100000,0x4000)
        endif()
        set(tool_rom ${MIME_ROM_${arch}}/${tool}.exe)
        if(tool IN_LIST MIME_TOOLS_WBEM)
            if(arch STREQUAL "x86")
                set(tool_rom ${MIME_ROM}/syswow64wbem/${tool}.exe)
            else()
                set(tool_rom ${MIME_ROM}/winwbem/${tool}.exe)
            endif()
        endif()
        mime_link(${MIME_OUT_${arch}}/${tool}.exe ${arch} OBJECTS ${tool_objects} LIBS ${tool_libs}
            FLAGS ${tool_flags} ROM ${tool_rom} GROUPS guest)
    endforeach()
endforeach()

# ===================================== features =====================================

# OptionalFeatures.exe, which turns the features that are fetched from the vendor on and off, and asks for them when a
# program needs one. It is in System32 under three names: dism.exe and fondue.exe are the same program, the names
# that scripts and programs written for Windows call. There is no 32-bit build: it writes the machine's registry and
# drive, and a 32-bit process would see both through the redirections of WOW64.
set(features_arch ${MIME_GUEST_ARCH})
mime_glob(features_sources guest/features/*.c guest/lib/features/*.c)
mime_objects(${features_arch}-features ${features_arch} SOURCES ${features_sources}
    INCLUDES guest/toolutil guest/features guest/lib/features)
set(features_libs ${MIME_LIB_${features_arch}}/ntdll.lib ${MIME_LIB_${features_arch}}/kernel32.lib
    ${MIME_LIB_${features_arch}}/advapi32.lib ${MIME_LIB_${features_arch}}/user32.lib ${MIME_LIB_${features_arch}}/win32u.lib
    ${MIME_LIB_${features_arch}}/comctl32.lib ${MIME_LIB_${features_arch}}/gdi32.lib ${MIME_LIB_${features_arch}}/winhttp.lib
    ${MIME_LIB_${features_arch}}/wintrust.lib ${MIME_LIB_${features_arch}}/crypt32.lib
    ${MIME_LIB_${features_arch}}/msi.lib)
foreach(features_name OptionalFeatures dism fondue)
    mime_link(${MIME_OUT_${features_arch}}/${features_name}.exe ${features_arch}
        OBJECTS ${features_arch}-features ${features_arch}-toolutil ${features_arch}-chkstk ${features_arch}-lib-cbs ${features_arch}-lib-msdelta
            ${features_arch}-lib-cabinet ${features_arch}-lib-png ${features_arch}-lib-lzms ${features_arch}-lib-crypto:guest/lib/crypto/hash.c LIBS ${features_libs}
        FLAGS /subsystem:console,10.0 /entry:entry /base:0x7FF6AE000000 /highentropyva /dynamicbase
            /stack:0x100000,0x4000
        ROM ${MIME_ROM_${features_arch}}/${features_name}.exe GROUPS guest)
endforeach()

# ===================================== cmd =====================================

# cmd.exe, the command interpreter: a console program of System32 and SysWOW64.
mime_glob(cmd_sources guest/cmd/*.c)
foreach(arch IN LISTS MIME_USER_ARCHS ITEMS x86)
    if(arch STREQUAL "x86" OR arch STREQUAL MIME_GUEST_ARCH)
        mime_objects(${arch}-cmd ${arch} SOURCES ${cmd_sources} INCLUDES guest/cmd)
        set(cmd_libs ${MIME_LIB_${arch}}/ntdll.lib ${MIME_LIB_${arch}}/kernel32.lib)
        set(cmd_objects ${arch}-cmd ${arch}-chkstk)
        set(cmd_flags /subsystem:console,10.0 /entry:entry /base:0x7FF6AD000000 /highentropyva /dynamicbase
            /stack:0x1000000,0x10000)
        if(arch STREQUAL "x86")
            list(APPEND cmd_objects x86-div)
            set(cmd_flags /subsystem:console,6.0 /entry:entry /base:0x00400000 /dynamicbase /stack:0x1000000,0x10000)
        endif()
        mime_link(${MIME_OUT_${arch}}/cmd.exe ${arch} OBJECTS ${cmd_objects} LIBS ${cmd_libs} FLAGS ${cmd_flags}
            RESOURCE cmd.exe ROM ${MIME_ROM_${arch}}/cmd.exe GROUPS guest)
    endif()
endforeach()

# ===================================== conhost =====================================

# conhost.exe, the program that draws the window of a console: a graphical program of System32.
mime_glob(conhost_sources guest/conhost/*.c)
foreach(arch IN LISTS MIME_USER_ARCHS)
    if(arch STREQUAL MIME_GUEST_ARCH)
        mime_objects(${arch}-conhost ${arch} SOURCES ${conhost_sources} INCLUDES guest/conhost)
        set(conhost_libs ${MIME_LIB_${arch}}/ntdll.lib ${MIME_LIB_${arch}}/kernel32.lib ${MIME_LIB_${arch}}/user32.lib
            ${MIME_LIB_${arch}}/gdi32.lib ${MIME_LIB_${arch}}/advapi32.lib)
        set(conhost_flags /subsystem:windows,10.0 /entry:entry /base:0x7FF6C6000000 /highentropyva /dynamicbase
            /stack:0x100000,0x4000)
        mime_link(${MIME_OUT_${arch}}/conhost.exe ${arch} OBJECTS ${arch}-conhost ${arch}-chkstk LIBS ${conhost_libs}
            FLAGS ${conhost_flags} RESOURCE conhost.exe RESOURCE_SCRIPT ${CMAKE_SOURCE_DIR}/guest/conhost/conhost.rc
            ROM ${MIME_ROM_${arch}}/conhost.exe GROUPS guest)
    endif()
endforeach()

# ===================================== registry =====================================

# the registry hives of a new machine, compiled from the sources of data/registry and carried in the ROM. the kernel loads
# them at boot, see guest/ntos/cm/registry.c.
add_executable(regc host/regc/regc.c)
target_compile_options(regc PRIVATE -Wall -Wextra -Wimplicit-fallthrough)
set_target_properties(regc PROPERTIES C_STANDARD 23 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tools)

mime_glob(registry_sources data/registry/*.reg)
set(MIME_HIVES ${CMAKE_BINARY_DIR}/guest/hives)
set(registry_hives ${MIME_HIVES}/SYSTEM ${MIME_HIVES}/SOFTWARE ${MIME_HIVES}/DEFAULT ${MIME_HIVES}/NTUSER.DAT)
add_custom_command(OUTPUT ${registry_hives}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${MIME_HIVES}
    COMMAND regc -o ${MIME_HIVES} ${registry_sources}
    DEPENDS regc ${registry_sources} COMMENT "Compiling the registry hives" VERBATIM)
set_property(GLOBAL APPEND PROPERTY MIME_ROM_FILES ${registry_hives})

# ===================================== fonts =====================================

# the folder's desktop.ini, which gives it the fonts view of the shell as on windows.
set_property(GLOBAL APPEND PROPERTY MIME_ROM_FILES ${CMAKE_SOURCE_DIR}/data/winfonts/desktop.ini)

# ===================================== typelibs =====================================

# The automation type library every other library imports, as Windows ships it: the 64-bit one as
# C:\Windows\System32\stdole2.tlb and the 32-bit one as C:\Windows\SysWOW64\stdole2.tlb.
set_property(GLOBAL APPEND PROPERTY MIME_ROM_FILES ${CMAKE_SOURCE_DIR}/data/wintypelibs/stdole2.tlb
    ${CMAKE_SOURCE_DIR}/data/syswow64/stdole2.tlb)

# ===================================== color =====================================

# the sRGB profile of the color folder, made by scripts/asset/icc.cmake, served from the ROM as
# C:\Windows\System32\spool\drivers\color\sRGB Color Space Profile.icm.
set(mime_srgb_profile "${MIME_ASSETS}/wincolor/sRGB Color Space Profile.icm")
add_custom_command(OUTPUT "${mime_srgb_profile}"
    COMMAND ${CMAKE_COMMAND} "-DOUTPUT=${mime_srgb_profile}" -P ${CMAKE_SOURCE_DIR}/scripts/asset/icc.cmake
    DEPENDS ${CMAKE_SOURCE_DIR}/scripts/asset/icc.cmake ${CMAKE_SOURCE_DIR}/scripts/asset/binary.cmake VERBATIM)
set_property(GLOBAL APPEND PROPERTY MIME_ROM_FILES "${mime_srgb_profile}")

# ===================================== theme =====================================

# the visual style of the machine, drawn by tools/gentheme.py of the research repository and committed in gen/themes, carried in the
# ROM as Windows\Resources\Themes\aero\aero.msstyles, see scripts/rom.cmake.
set_property(GLOBAL APPEND PROPERTY MIME_ROM_FILES ${CMAKE_SOURCE_DIR}/gen/themes/aero/aero.msstyles)

# ===================================== host =====================================

get_property(rom_files GLOBAL PROPERTY MIME_ROM_FILES)
# microsoft's originals of a few system files are fetched by the host at run time, never put in the build: the rom and
# the host only know them by the manifest of the guest's windows, see host/genuine.c.
set(genuine_manifest ${CMAKE_SOURCE_DIR}/data/genuine-${MIME_GUEST_ARCH}.manifest)
add_custom_command(OUTPUT ${MIME_BUILD_GEN}/rom.S
    COMMAND ${CMAKE_COMMAND} -DOUTPUT=${MIME_BUILD_GEN}/rom.S -DSYSTEM=${CMAKE_SYSTEM_NAME} "-DFILES=${rom_files}"
        -DMANIFEST=${genuine_manifest} -P ${CMAKE_SOURCE_DIR}/scripts/rom.cmake
    DEPENDS scripts/rom.cmake ${rom_files} ${genuine_manifest} VERBATIM)
add_custom_target(rom DEPENDS ${MIME_BUILD_GEN}/rom.S)

mime_group(guest ALL DEPENDS rom)

if(NOT MIME_BUILD_HOST)
    return()
endif()

set(host_warnings -Wall -Wextra -Wimplicit-fallthrough -Wno-unused-parameter)
set(host_gnu ON)
if(APPLE)
    set(host_gnu OFF)
endif()

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

set(vulkan_gen ${MIME_GEN}/vulkan)
add_library(mvk-host OBJECT ${vulkan_gen}/mvk_structs.c ${vulkan_gen}/mvk_host_commands.c)
target_include_directories(mvk-host PRIVATE third_party/khronos ${vulkan_gen} host/vulkan include)
target_compile_options(mvk-host PRIVATE ${host_warnings} -Wno-unused-variable -Wno-unused-function)
set_target_properties(mvk-host PROPERTIES C_STANDARD 23 C_EXTENSIONS ${host_gnu})
if(NOT APPLE)
    target_compile_definitions(mvk-host PRIVATE _GNU_SOURCE)
endif()
mime_glob(vulkan_host_sources host/vulkan/*.c)
set_source_files_properties(host/vulkan/gamma.c PROPERTIES
    INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/third_party/glslang")
set_source_files_properties(host/vulkan/vulkan.c host/vulkan/budget.c host/vulkan/vkshare.c host/vulkan/vkwsi.c
    host/vulkan/video.c host/vulkan/video_h264.c host/vulkan/video_h265.c host/vulkan/video_vp9.c
    host/vulkan/video_av1.c PROPERTIES
    COMPILE_OPTIONS -Wno-missing-field-initializers
    INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/third_party/khronos;${vulkan_gen}")

if(APPLE)
    enable_language(OBJC)
    mime_glob(host_sources host/*.c host/vmm/*.c host/ui/*.c host/ui/*.m host/gpu/*.c host/gpu/*.m
        host/audio/*.c host/camera/*.c)

    mime_glob(vkmetal_sources host/vkmetal/*.m host/vkmetal/*.cpp)
    # the driver keeps its metal objects in vulkan's c objects, which own them by hand as gpu.m does.
    set_source_files_properties(${vkmetal_sources} PROPERTIES COMPILE_OPTIONS "-Wno-missing-field-initializers;-fno-objc-arc"
        INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/third_party/khronos;${CMAKE_SOURCE_DIR}/third_party/SPIRV-Cross")
    set_source_files_properties(host/vkmetal/msl.cpp PROPERTIES COMPILE_OPTIONS "-std=c++17;-Wno-missing-field-initializers")

    add_executable(mime ${host_sources} ${vulkan_host_sources} ${vkmetal_sources} ${MIME_BUILD_GEN}/rom.S
        $<TARGET_OBJECTS:mvk-host> $<TARGET_OBJECTS:shaderc>)
    set_source_files_properties(host/gpu/gpu.m host/gpu/shader.c PROPERTIES COMPILE_OPTIONS -fblocks
        INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/third_party;${CMAKE_SOURCE_DIR}/third_party/glslang")
    # the replay keeps its metal objects in c tables it grows with realloc, which arc cannot own.
    set_property(SOURCE host/gpu/gpu.m APPEND PROPERTY COMPILE_OPTIONS -fno-objc-arc)
    target_link_libraries(mime PRIVATE "-framework Hypervisor" "-framework AppKit" "-framework QuartzCore"
        "-framework CoreGraphics" "-framework ImageIO" "-framework Metal" "-framework MetalFX" "-framework IOSurface" "-framework Carbon"
        "-framework AudioToolbox" "-framework CoreAudio" "-framework Security" objc)
    set_target_properties(mime PROPERTIES LINKER_LANGUAGE CXX)
    add_custom_command(TARGET mime POST_BUILD
        COMMAND codesign -s - -f --entitlements ${CMAKE_SOURCE_DIR}/host/mime.entitlements $<TARGET_FILE:mime>
        VERBATIM)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(HOST_LIBS REQUIRED IMPORTED_TARGET sdl3 wayland-client)
    pkg_check_modules(HOST_SDBUS IMPORTED_TARGET libsystemd)
    find_package(Threads REQUIRED)
    mime_glob(host_sources host/*.c host/linux/*.c host/vmm/*.c host/camera/*.c)
    list(FILTER host_sources EXCLUDE REGEX "/host/vmm/(vm|vcpu|lazy|loader|window)[.]c$")
    list(APPEND host_sources host/gpu/shader.c host/gpu/glsl_compat.c host/ui/pe_icon.c)

    add_executable(mime ${host_sources} ${vulkan_host_sources} ${MIME_BUILD_GEN}/rom.S $<TARGET_OBJECTS:mvk-host>
        $<TARGET_OBJECTS:shaderc>)
    set_source_files_properties(host/gpu/shader.c PROPERTIES
        INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/third_party;${CMAKE_SOURCE_DIR}/third_party/glslang")
    set_source_files_properties(host/linux/gpu.c PROPERTIES COMPILE_OPTIONS -Wno-missing-field-initializers
        INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/third_party/khronos;${vulkan_gen};${CMAKE_SOURCE_DIR}/third_party/glslang")
    target_compile_definitions(mime PRIVATE _GNU_SOURCE)
    target_link_libraries(mime PRIVATE PkgConfig::HOST_LIBS Threads::Threads m)
    # the client tells a machine of another build by it, see host/build_id.c.
    target_link_options(mime PRIVATE -Wl,--build-id=sha1)
    if(HOST_SDBUS_FOUND)
        target_compile_definitions(mime PRIVATE MIME_HAVE_SDBUS=1)
        target_link_libraries(mime PRIVATE PkgConfig::HOST_SDBUS)
    else()
        message(STATUS "mime: no libsystemd, the host will not show the guest's notification icons")
    endif()
    # only the headers: the library is loaded at run time, so a host without PipeWire still starts.
    pkg_check_modules(HOST_PIPEWIRE libpipewire-0.3)
    if(HOST_PIPEWIRE_FOUND)
        set(pipewire_includes "")
        foreach(dir ${HOST_PIPEWIRE_INCLUDE_DIRS})
            list(APPEND pipewire_includes "-isystem${dir}")
        endforeach()
        set_source_files_properties(host/linux/pipewire.c PROPERTIES COMPILE_DEFINITIONS MIME_HAVE_PIPEWIRE=1
            COMPILE_OPTIONS "${pipewire_includes}")
        target_link_libraries(mime PRIVATE ${CMAKE_DL_LIBS})
    else()
        message(STATUS "mime: no libpipewire headers, audio devices get the formats SDL tells")
    endif()
    set_target_properties(mime PROPERTIES LINKER_LANGUAGE CXX)
endif()

target_include_directories(mime PRIVATE include ${MIME_GEN})
set_source_files_properties(host/main.c PROPERTIES COMPILE_DEFINITIONS MIME_VERSION_STRING="${PROJECT_VERSION}")
target_compile_options(mime PRIVATE $<$<COMPILE_LANGUAGE:C,OBJC>:${host_warnings}>)
target_compile_options(mime PRIVATE $<$<COMPILE_LANGUAGE:OBJC>:-fobjc-arc>)
set_target_properties(mime PROPERTIES C_STANDARD 23 C_STANDARD_REQUIRED ON C_EXTENSIONS ${host_gnu}
    OBJC_STANDARD 23 OBJC_STANDARD_REQUIRED ON OBJC_EXTENSIONS ${host_gnu}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set_source_files_properties(${MIME_BUILD_GEN}/rom.S PROPERTIES OBJECT_DEPENDS "${rom_files}")
add_dependencies(mime rom)

# the manifest as a string of the host, which fetches the files it lists and checks each against its hash.
add_custom_command(OUTPUT ${MIME_BUILD_GEN}/genuine_manifest.h
    COMMAND ${CMAKE_COMMAND} -DINPUT=${genuine_manifest} -DOUTPUT=${MIME_BUILD_GEN}/genuine_manifest.h
        -P ${CMAKE_SOURCE_DIR}/scripts/genuine.cmake
    DEPENDS scripts/genuine.cmake ${genuine_manifest} VERBATIM)
target_sources(mime PRIVATE ${MIME_BUILD_GEN}/genuine_manifest.h)
set_source_files_properties(host/genuine.c PROPERTIES INCLUDE_DIRECTORIES ${MIME_BUILD_GEN}
    OBJECT_DEPENDS ${MIME_BUILD_GEN}/genuine_manifest.h)
find_package(CURL REQUIRED)
# the sdk's usr/include as -isystem comes before libc++'s headers and breaks them, and the compiler searches it already.
if(CURL_INCLUDE_DIR MATCHES "\\.sdk/usr/include$")
    set_target_properties(CURL::libcurl PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "")
endif()
target_link_libraries(mime PRIVATE CURL::libcurl)
