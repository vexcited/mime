cmake_minimum_required(VERSION 3.30)

# cmake 4.5 imports the C++23 std module into every target whose scanning is on. nothing here uses modules, and the
# windows targets have no libstdc++ include path, so the scanner's std module cannot be built for them.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

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
# CMake 4.5 builds the std module for every scanned C++20 target, which the guest toolchain has no headers for.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)


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

# The files of Windows the build makes: the icons of Mime's own programs, drawn in guest/<program>/icons, and the
# message tables of data/messages. The icons, cursors and sounds of Windows are Microsoft's, see the genuine section.
set(MIME_ASSETS ${MIME_BUILD_GEN}/assets)
set(MIME_ASSET_SCRIPTS ${CMAKE_SOURCE_DIR}/scripts/asset/binary.cmake ${CMAKE_SOURCE_DIR}/scripts/asset/raster.cmake
    ${CMAKE_SOURCE_DIR}/scripts/asset/png.cmake)

set(mime_messages_kernelbase win32 hresult)
set(mime_messages_ntdll ntstatus)

function(_mime_icon_command output kind images depends)
    add_custom_command(OUTPUT ${output}
        COMMAND ${CMAKE_COMMAND} -DKIND=${kind} -DOUTPUT=${output} "-DIMAGES=${images}" ${ARGN}
            -P ${CMAKE_SOURCE_DIR}/scripts/asset/icon.cmake
        DEPENDS ${depends} ${CMAKE_SOURCE_DIR}/scripts/asset/icon.cmake ${MIME_ASSET_SCRIPTS} VERBATIM)
endfunction()

# The file a resource script names as @MIME_ASSETS@/<path>, and the rule that makes it.
function(mime_asset path)
    get_property(done GLOBAL PROPERTY MIME_ASSET_${path})
    if(done)
        return()
    endif()
    set_property(GLOBAL PROPERTY MIME_ASSET_${path} TRUE)
    set(output ${MIME_ASSETS}/${path})
    if(path MATCHES "^programs/(.+)/(.+)\\.ico$")
        # <name>_<size>.svg, one drawing for each size of the icon.
        file(GLOB images CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/guest/${CMAKE_MATCH_1}/icons/${CMAKE_MATCH_2}_*.svg)
        if(NOT images)
            message(FATAL_ERROR "guest/${CMAKE_MATCH_1}/icons has no drawing of ${path}")
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

# ===================================== genuine =====================================

# the system files of windows that are microsoft's own, which the host fetches and holds: mime has no code for them and
# the build none of their bytes. the manifest of the guest's windows lists them, see host/genuine.c, and the rom names
# each one with no bytes, see scripts/rom.cmake. mime's binaries link against import libraries made from the lists of
# their exports: gen/<arch>/<name>.def where the processors differ, gen/implib/<name>.def where they do not, and
# gen/x86/<name>.imports.def.
set(genuine_manifest ${CMAKE_SOURCE_DIR}/data/genuine-${MIME_GUEST_ARCH}.manifest)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${genuine_manifest})
set(MIME_GENUINE_FILES)
file(STRINGS ${genuine_manifest} genuine_lines)
foreach(line IN LISTS genuine_lines)
    if(line MATCHES "^([^\t#][^\t]*)\t")
        string(TOLOWER "${CMAKE_MATCH_1}" genuine_path)
        list(APPEND MIME_GENUINE_FILES "${genuine_path}")
    endif()
endforeach()

# whether file of the system folder of arch is one of them. an x64 program on windows on arm loads the arm64x file of
# System32.
function(mime_genuine out arch file)
    set(directory system32)
    if(arch STREQUAL "x86")
        set(directory syswow64)
    endif()
    string(TOLOWER "${directory}/${file}" path)
    if(path IN_LIST MIME_GENUINE_FILES)
        set(${out} TRUE PARENT_SCOPE)
    else()
        set(${out} FALSE PARENT_SCOPE)
    endif()
endfunction()

# ===================================== guest =====================================

set(MIME_TRIPLE_x64 --target=x86_64-pc-windows-msvc -march=x86-64 -mcx16)
set(MIME_TRIPLE_arm64 --target=aarch64-pc-windows-msvc -march=armv8.2-a)
# programs that hook the 32-bit system libraries, such as the Steam overlay, expect the five byte start that
# Windows gives every function there, and cut a longer first instruction short when they move it.
set(MIME_TRIPLE_x86 --target=i686-pc-windows-msvc -march=pentium4 -fms-hotpatch -fno-omit-frame-pointer)
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
    # like the programs of windows, the programs of mime carry a load configuration with a security cookie. the dlls
    # get theirs in mime_dll.
    if(NOT arch STREQUAL "x86" AND NOT "/dll" IN_LIST arg_FLAGS AND "${arg_FLAGS}" MATCHES "/subsystem:(console|windows)")
        list(APPEND arg_OBJECTS ${arch}-loadconfig)
    endif()
    # what calls win32k may call a service only mime has, which no win32u.dll exports: see guest/win32u/mime_x64.S
    # and guest/win32u/mime_x86.S.
    if((arch STREQUAL "x64" OR arch STREQUAL "x86") AND "${arg_LIBS}" MATCHES "/win32u\\.lib")
        list(APPEND arg_OBJECTS ${arch}-win32kmime)
    endif()
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

mime_objects(kernel-ntos kernel SOURCES ${ntos_common} ${ntos_arch} ${ntos_asm} guest/lib/crypto/hash.c guest/lib/crypto/bignum.c
    guest/lib/crypto/rsa.c
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

set(MIME_CHKSTK_arm64 guest/crt/chkstk.S)
set(MIME_CHKSTK_x64 guest/crt/x64/chkstk.S)
set(MIME_CHKSTK_x86 guest/crt/x86/chkstk.S)
foreach(arch IN LISTS MIME_USER_ARCHS ITEMS x86)
    mime_objects(${arch}-rtl ${arch} SOURCES ${rtl_sources})
    mime_objects(${arch}-crt ${arch} SOURCES ${crt_sources} OPTIONS -fno-builtin)
    mime_objects(${arch}-chkstk ${arch} SOURCES ${MIME_CHKSTK_${arch}})
    mime_objects(${arch}-loadconfig ${arch} SOURCES guest/crt/dll/loadconfig.c)
    set(lib_includes guest/lib third_party/SPIRV-Cross)
    foreach(lib privdata dxbc dxil hlsl png pngio jpeg tiff gif bcn vector msstyles compound cabinet audio dinput xinput richedit crypto ttf xmldom cbs lzms wim msdelta winrt icc par transfer)
        mime_glob(lib_sources guest/lib/${lib}/*.c)
        mime_objects(${arch}-lib-${lib} ${arch} SOURCES ${lib_sources} INCLUDES ${lib_includes})
    endforeach()
    # damaged jpeg xr data can overflow the integer transforms, which must wrap.
    mime_glob(lib_sources guest/lib/jxr/*.c)
    mime_objects(${arch}-lib-jxr ${arch} SOURCES ${lib_sources} INCLUDES ${lib_includes} OPTIONS -fwrapv)
endforeach()
mime_objects(x86-div x86 SOURCES guest/crt/x86/div.c guest/crt/x86/div_entry.S)
mime_objects(x86-win32kmime x86 SOURCES guest/win32u/mime_x86.S)
set(MIME_RUNTIME_arm64 arm64-rtl arm64-crt arm64-chkstk)
set(MIME_RUNTIME_x64 x64-rtl x64-crt x64-chkstk)
set(MIME_RUNTIME_x86 x86-rtl x86-crt x86-chkstk x86-div)

# the import library of a system file that is microsoft's, made from the list of its exports alone.
function(mime_implib name arch)
    cmake_parse_arguments(PARSE_ARGV 2 arg "" "FILE;DEF" "")
    if(NOT arg_FILE)
        set(arg_FILE ${name}.dll)
    endif()
    mime_genuine(genuine ${arch} ${arg_FILE})
    if(NOT genuine)
        message(FATAL_ERROR "mime has no code for the ${arch} ${arg_FILE}, which ${genuine_manifest} does not list")
    endif()
    set(def ${arg_DEF})
    if(NOT def AND arch STREQUAL "x86")
        set(def ${MIME_GEN}/x86/${name}.imports.def)
    elseif(NOT def AND EXISTS ${MIME_GEN}/${arch}/${name}.def)
        set(def ${MIME_GEN}/${arch}/${name}.def)
    elseif(NOT def)
        set(def ${MIME_GEN}/implib/${name}.def)
    endif()
    set(implib ${MIME_LIB_${arch}}/${name}.lib)
    if(arch STREQUAL "x86")
        set(command ${MIME_LLVM_DLLTOOL} -m i386 -k -d ${def} -l ${implib})
    else()
        set(command ${MIME_LLD_LINK} /lib /nologo /machine:${MIME_MACHINE_${arch}} /def:${def} /out:${implib})
    endif()
    file(RELATIVE_PATH comment ${CMAKE_BINARY_DIR} ${implib})
    add_custom_command(OUTPUT ${implib} COMMAND ${command} DEPENDS ${def}
        COMMENT "Making ${comment}" VERBATIM)
    set_property(GLOBAL APPEND PROPERTY MIME_GROUP_guest ${implib})
endfunction()

function(mime_dll name arch)
    cmake_parse_arguments(PARSE_ARGV 2 arg "NOIMPLIB" "BASE;FILE;ENTRY;DEF;IMPORTS_DEF"
        "SOURCES;OBJECTS;OPTIONS;INCLUDES;IMPORTS;LIBS;FLAGS;DEPENDS")
    if(NOT arg_FILE)
        set(arg_FILE ${name}.dll)
    endif()
    # where the file is microsoft's, mime's code for it is not built: only what links against it is made.
    mime_genuine(genuine ${arch} ${arg_FILE})
    if(genuine)
        if(NOT arg_NOIMPLIB AND arch STREQUAL "x86")
            mime_implib(${name} x86 FILE ${arg_FILE} DEF ${arg_IMPORTS_DEF})
        elseif(NOT arg_NOIMPLIB)
            mime_implib(${name} ${arch} FILE ${arg_FILE})
        endif()
        return()
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
    # the 32-bit dlls say subsystem 6.0, and a load configuration there must list the handlers of their exceptions.
    if(NOT arch STREQUAL "x86")
        list(APPEND objects ${arch}-loadconfig)
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
        mime_objects(x64-win32kmime x64 SOURCES guest/win32u/mime_x64.S)
    endif()

    # microsoft's on every machine, see the genuine section.
    foreach(name ntdll kernel32 gdi32 user32 msvcrt ucrtbase bcrypt oleaut32)
        mime_implib(${name} ${arch})
    endforeach()
    mime_implib(win32u ${arch} DEF ${MIME_GEN}/win32u.def)

    mime_sources(sources kernelbase ${arch})
    # the ntdll of windows compiles no manifest, so kernelbase carries the generator of activation contexts itself.
    list(APPEND sources guest/ntdll/actgen.c guest/ntdll/manifest.c)
    mime_dll(kernelbase ${arch} BASE 0x7FFB00000000 SOURCES ${sources} OBJECTS ${arch}-chkstk
        OPTIONS -DMIME_BUILD_KERNELBASE IMPORTS ntdll)

    mime_dll(mscoree ${arch} BASE 0x7FF7B0000000 SOURCES guest/mscoree/main.c guest/mscoree/${arch}/common.S ${MIME_GEN}/${arch}/mscoree_thunks.S OBJECTS ${arch}-chkstk
        IMPORTS ntdll kernel32 DEF ${MIME_GEN}/${arch}/mscoree.def)

endforeach()

if(MIME_GUEST_ARCH STREQUAL "arm64")
    mime_glob(sources guest/xta64/*.c guest/xta64/*.S)
    mime_dll(xta64 arm64 BASE 0x7FFD00000000 ENTRY /noentry SOURCES ${sources}
        OBJECTS arm64-ntsyscalls ${MIME_RUNTIME_arm64} OPTIONS -fno-math-errno
        FLAGS /export:RtlUserThreadStart=MxUserThreadStart NOIMPLIB)
endif()

set(MIME_EXTRA_DLLS advapi32:0x7FF6E0000000 sechost:0x7FF080000000 ws2_32:0x7FF6D0000000 iphlpapi:0x7FF6C0000000
    dnsapi:0x7FF6B0000000 ole32:0x7FF6A0000000 rpcrt4:0x7FF680000000
    shell32:0x7FF670000000 shlwapi:0x7FF660000000 comdlg32:0x7FF650000000 comctl32:0x7FF640000000
    crypt32:0x7FF630000000 schannel:0x7FF620000000 secur32:0x7FF610000000 uxtheme:0x7FF600000000
    imm32:0x7FF5F0000000 usp10:0x7FF5E0000000 dwmapi:0x7FF5D0000000 winmm:0x7FF5C0000000
    version:0x7FF5B0000000 userenv:0x7FF5A0000000 wtsapi32:0x7FF590000000 netapi32:0x7FF580000000
    odbc32:0x7FF570000000 winspool:0x7FF560000000 msi:0x7FF550000000 dbghelp:0x7FF540000000 imagehlp:0x7FF100000000 mswsock:0x7FF720000000 winsta:0x7FF730000000
    avrt:0x7FF520000000 dinput8:0x7FF510000000 dwrite:0x7FF500000000
    dxgi:0x7FF4F0000000 wsock32:0x7FF4E0000000 mpr:0x7FF4C0000000 wintrust:0x7FF4B0000000
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
list(APPEND MIME_EXTRA_DLLS sensapi:0x7FEBF0000000 verifier:0x7FEBE0000000 wldp:0x7FECD0000000 rasapi32:0x7FEC10000000 uianimation:0x7FECB0000000
    windows.graphics:0x7FECA0000000 windows.media.devices:0x7FEB60000000 windows.devices.enumeration:0x7FEB50000000 windows.networking.connectivity:0x7FEB40000000 cryptowinrt:0x7FEB30000000 cryptnet:0x7FEC20000000 msdmo:0x7FEC30000000 dxva2:0x7FEC40000000 gpapi:0x7FEC60000000 cryptbase:0x7FEC50000000 evr:0x7FEBD0000000
    twinapi.appcore:0x7FEBC0000000 fwpuclnt:0x7FEBB0000000 cryptsvc:0x7FEBA0000000 nlansp_c:0x7FEB90000000 dosvc:0x7FEB80000000
    qmgr:0x7FEB70000000 netprofmsvc:0x7FEB20000000 bthserv:0x7FEB10000000 devmgr:0x7FEB00000000 newdev:0x7FEAF0000000
    ngcsvc:0x7FEAE0000000 dhcpcore:0x7FEAD0000000 dnsrslvr:0x7FEAC0000000)

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
    set(libs_oledlg ${MIME_LIB_${arch}}/ole32.lib ${MIME_LIB_${arch}}/advapi32.lib)
    set(libs_netapi32 ${MIME_LIB_${arch}}/advapi32.lib)
    set(libs_userenv ${MIME_LIB_${arch}}/advapi32.lib)
    set(libs_uianimation ${MIME_LIB_${arch}}/msvcrt.lib)
    set(libs_mlang ${MIME_LIB_${arch}}/oleaut32.lib)
    set(libs_secur32 ${MIME_LIB_${arch}}/advapi32.lib)
    set(libs_crypt32 ${MIME_LIB_${arch}}/rpcrt4.lib)
    set(libs_rsaenh ${MIME_LIB_${arch}}/advapi32.lib)
    set(objects_windowscodecs ${arch}-lib-png:guest/lib/png/inflate.c ${arch}-lib-png:guest/lib/png/deflate.c
        ${arch}-lib-pngio ${arch}-lib-jpeg ${arch}-lib-tiff ${arch}-lib-gif ${arch}-lib-bcn ${arch}-lib-icc ${arch}-lib-jxr ${arch}-lib-par)
    set(includes_windowscodecs guest/lib)
    set(libs_windowscodecs ${MIME_LIB_${arch}}/ole32.lib)
    set(objects_dwrite ${arch}-lib-ttf ${arch}-lib-png:guest/lib/png/inflate.c)
    set(includes_dwrite guest/lib)
    set(objects_combase ${arch}-lib-crypto:guest/lib/crypto/hash.c)
    set(includes_combase guest/lib)
    set(objects_crypt32 ${arch}-lib-crypto ${arch}-lib-compound ${arch}-lib-cabinet)
    set(objects_secur32 ${arch}-lib-crypto)
    set(objects_ncrypt ${arch}-lib-crypto:guest/lib/crypto/aes.c ${arch}-lib-crypto:guest/lib/crypto/des.c ${arch}-lib-crypto:guest/lib/crypto/hash.c ${arch}-lib-crypto:guest/lib/crypto/hmac.c ${arch}-lib-crypto:guest/lib/crypto/kdf.c ${arch}-lib-crypto:guest/lib/crypto/modes.c)
    set(includes_ncrypt guest/lib)
    set(objects_advapi32 ${arch}-lib-crypto:guest/lib/crypto/hash.c ${arch}-lib-crypto:guest/lib/crypto/stream.c
        ${arch}-lib-crypto:guest/lib/crypto/bignum.c ${arch}-lib-crypto:guest/lib/crypto/rsa.c
        ${arch}-lib-crypto:guest/lib/crypto/aes.c ${arch}-lib-crypto:guest/lib/crypto/des.c
        ${arch}-lib-crypto:guest/lib/crypto/modes.c ${arch}-lib-crypto:guest/lib/crypto/kdf.c ${arch}-lib-crypto:guest/lib/crypto/hmac.c)
    # where advapi32 is microsoft's, cryptsp has the providers of mime's advapi32, see guest/cryptsp/crypt.c.
    set(objects_cryptsp ${objects_advapi32})
    set(libs_cryptsp ${MIME_LIB_${arch}}/advapi32.lib)
    foreach(crypto crypt32 secur32 advapi32 cryptsp)
        set(includes_${crypto} guest/lib)
    endforeach()
    set(objects_riched20 ${arch}-lib-richedit)
    set(objects_msftedit ${arch}-lib-richedit)
    set(includes_riched20 guest/lib)
    set(includes_msftedit guest/lib)
    set(libs_riched32 ${MIME_LIB_${arch}}/riched20.lib)
    set(libs_ole32 ${MIME_LIB_${arch}}/rpcrt4.lib)
    set(objects_winhttp ${arch}-lib-png:guest/lib/png/inflate.c)
    set(objects_winspool ${arch}-lib-png:guest/lib/png/deflate.c ${arch}-lib-png:guest/lib/png/inflate.c)
    set(includes_winspool guest/lib)
    set(includes_winhttp guest/lib)
    set(libs_winhttp ${MIME_LIB_${arch}}/ws2_32.lib)
    set(libs_wsock32 ${MIME_LIB_${arch}}/ws2_32.lib)
    set(libs_dnsapi ${MIME_LIB_${arch}}/ws2_32.lib ${MIME_LIB_${arch}}/rpcrt4.lib)
    set(objects_wininet ${arch}-lib-png:guest/lib/png/inflate.c)
    set(includes_wininet guest/lib)
    set(libs_wininet ${MIME_LIB_${arch}}/ws2_32.lib ${MIME_LIB_${arch}}/kernelbase.lib)
    set(libs_urlmon ${MIME_LIB_${arch}}/wininet.lib ${MIME_LIB_${arch}}/ole32.lib ${MIME_LIB_${arch}}/oleaut32.lib ${MIME_LIB_${arch}}/kernelbase.lib)
    set(objects_dxgi ${arch}-lib-privdata)
    set(objects_d3d11 ${arch}-lib-privdata ${arch}-lib-dxbc)
    set(objects_d3d12 ${arch}-lib-privdata ${arch}-lib-dxbc ${arch}-lib-dxil ${arch}-d3d11:guest/d3d11/format.c ${arch}-d3d11:guest/d3d11/vkprofile.c ${arch}-d3d11:guest/d3d11/gfxshare.c)
    set(objects_d3dcompiler_47 ${arch}-lib-dxbc:guest/lib/dxbc/container.c ${arch}-lib-dxbc:guest/lib/dxbc/reader.c ${arch}-lib-hlsl)
    set(objects_d3d9 ${arch}-lib-privdata ${arch}-lib-dxbc:guest/lib/dxbc/container.c)
    set(objects_d3d10 ${arch}-lib-dxbc:guest/lib/dxbc/container.c)
    set(objects_d3d10_1 ${arch}-d3d10:guest/d3d10/create.c)
    set(objects_d3d10_1core ${arch}-d3d10core:guest/d3d10core/kmt.c)
    set(libs_wintrust ${MIME_LIB_${arch}}/crypt32.lib ${MIME_LIB_${arch}}/imagehlp.lib)
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
    set(libs_onecorecommonproxystub ${MIME_LIB_${arch}}/ole32.lib ${MIME_LIB_${arch}}/oleaut32.lib ${MIME_LIB_${arch}}/rpcrt4.lib)
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
        ${MIME_LIB_${arch}}/msvcrt.lib ${MIME_LIB_${arch}}/ucrtbase.lib)
    set(libs_propsys ${MIME_LIB_${arch}}/ole32.lib ${MIME_LIB_${arch}}/oleaut32.lib ${MIME_LIB_${arch}}/advapi32.lib
        ${MIME_LIB_${arch}}/shlwapi.lib ${MIME_LIB_${arch}}/combase.lib)
    set(objects_propsys ${arch}-lib-crypto:guest/lib/crypto/hash.c)
    set(includes_propsys guest/lib)
    set(libs_shcore ${MIME_LIB_${arch}}/ole32.lib ${MIME_LIB_${arch}}/advapi32.lib)
    set(libs_shlwapi ${MIME_LIB_${arch}}/shcore.lib)
    set(objects_shcore ${arch}-lib-winrt)
    set(includes_shcore guest/lib)
    foreach(runtime windows.media.devices windows.devices.enumeration windows.networking.connectivity cryptowinrt)
        set(includes_${runtime} guest/lib)
        set(libs_${runtime} ${MIME_LIB_${arch}}/combase.lib)
    endforeach()
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
    set(libs_dxva2 ${MIME_LIB_${arch}}/ole32.lib)
    set(libs_evr ${MIME_LIB_${arch}}/mfplat.lib ${MIME_LIB_${arch}}/ole32.lib ${MIME_LIB_${arch}}/gdi32.lib
        ${MIME_LIB_${arch}}/user32.lib ${MIME_LIB_${arch}}/dxva2.lib ${MIME_LIB_${arch}}/d3d9.lib)
    set(libs_comctl32 ${MIME_LIB_${arch}}/uxtheme.lib)
    set(libs_msctf ${MIME_LIB_${arch}}/ole32.lib ${MIME_LIB_${arch}}/oleaut32.lib)
    set(libs_oleacc ${MIME_LIB_${arch}}/ole32.lib ${MIME_LIB_${arch}}/oleaut32.lib)
    set(libs_uiautomationcore ${MIME_LIB_${arch}}/ole32.lib ${MIME_LIB_${arch}}/oleaut32.lib ${MIME_LIB_${arch}}/oleacc.lib
        ${MIME_LIB_${arch}}/advapi32.lib)
    foreach(service rpcss rpcepmap coremessaging)
        set(libs_${service} ${MIME_LIB_${arch}}/advapi32.lib)
    endforeach()
    list(APPEND libs_coremessaging ${MIME_LIB_${arch}}/ole32.lib ${MIME_LIB_${arch}}/combase.lib)
    list(APPEND libs_rpcepmap ${MIME_LIB_${arch}}/rpcrt4.lib)
    list(APPEND libs_rpcss ${MIME_LIB_${arch}}/rpcrt4.lib ${MIME_LIB_${arch}}/iphlpapi.lib)
    set(libs_wkscli ${MIME_LIB_${arch}}/advapi32.lib ${MIME_LIB_${arch}}/rpcrt4.lib)
    set(libs_wkssvc ${MIME_LIB_${arch}}/advapi32.lib ${MIME_LIB_${arch}}/rpcrt4.lib)
    set(libs_nsisvc ${MIME_LIB_${arch}}/advapi32.lib ${MIME_LIB_${arch}}/rpcrt4.lib)
    set(libs_keyiso ${MIME_LIB_${arch}}/advapi32.lib)
    set(libs_cryptsvc ${MIME_LIB_${arch}}/advapi32.lib ${MIME_LIB_${arch}}/rpcrt4.lib)
    set(libs_bthserv ${MIME_LIB_${arch}}/advapi32.lib ${MIME_LIB_${arch}}/rpcrt4.lib ${MIME_LIB_${arch}}/ole32.lib
        ${MIME_LIB_${arch}}/oleaut32.lib)
    set(libs_dhcpcore ${MIME_LIB_${arch}}/advapi32.lib ${MIME_LIB_${arch}}/rpcrt4.lib ${MIME_LIB_${arch}}/iphlpapi.lib)
    set(libs_dnsrslvr ${MIME_LIB_${arch}}/advapi32.lib ${MIME_LIB_${arch}}/rpcrt4.lib ${MIME_LIB_${arch}}/ws2_32.lib)
    set(libs_ngcsvc ${MIME_LIB_${arch}}/advapi32.lib ${MIME_LIB_${arch}}/rpcrt4.lib ${MIME_LIB_${arch}}/ncrypt.lib)
    set(libs_devmgr ${MIME_LIB_${arch}}/advapi32.lib ${MIME_LIB_${arch}}/comctl32.lib ${MIME_LIB_${arch}}/setupapi.lib
        ${MIME_LIB_${arch}}/cfgmgr32.lib ${MIME_LIB_${arch}}/uxtheme.lib)
    set(libs_newdev ${MIME_LIB_${arch}}/setupapi.lib ${MIME_LIB_${arch}}/shell32.lib)
    set(objects_dosvc ${arch}-lib-transfer)
    set(includes_dosvc guest/lib)
    set(libs_dosvc ${MIME_LIB_${arch}}/advapi32.lib ${MIME_LIB_${arch}}/ole32.lib ${MIME_LIB_${arch}}/oleaut32.lib
        ${MIME_LIB_${arch}}/winhttp.lib ${MIME_LIB_${arch}}/crypt32.lib)
    set(objects_qmgr ${arch}-lib-transfer)
    set(includes_qmgr guest/lib)
    set(libs_qmgr ${MIME_LIB_${arch}}/advapi32.lib ${MIME_LIB_${arch}}/ole32.lib ${MIME_LIB_${arch}}/oleaut32.lib
        ${MIME_LIB_${arch}}/rpcrt4.lib ${MIME_LIB_${arch}}/winhttp.lib ${MIME_LIB_${arch}}/crypt32.lib
        ${MIME_LIB_${arch}}/kernelbase.lib)
    set(libs_netprofmsvc ${MIME_LIB_${arch}}/advapi32.lib ${MIME_LIB_${arch}}/ole32.lib ${MIME_LIB_${arch}}/iphlpapi.lib)
    set(libs_nlansp_c ${MIME_LIB_${arch}}/ws2_32.lib ${MIME_LIB_${arch}}/iphlpapi.lib)
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
        elseif(name STREQUAL "windows.media.devices")
            set(file Windows.Media.Devices.dll)
        elseif(name STREQUAL "windows.devices.enumeration")
            set(file Windows.Devices.Enumeration.dll)
        elseif(name STREQUAL "windows.networking.connectivity")
            set(file Windows.Networking.Connectivity.dll)
        elseif(name STREQUAL "cryptowinrt")
            set(file CryptoWinRT.dll)
        endif()
        mime_glob(sources guest/${name}/*.c guest/${name}/*.cpp guest/${name}/${arch}/*.S)
        # the classes are those mime's combase had, each compiled into the library windows keeps it in.
        if(name STREQUAL "windows.media.devices")
            list(APPEND sources guest/combase/mediadevice.c guest/combase/audioendpoint.c)
        elseif(name STREQUAL "windows.devices.enumeration")
            list(APPEND sources guest/combase/devinfo.c guest/combase/audioendpoint.c)
        elseif(name STREQUAL "windows.networking.connectivity")
            list(APPEND sources guest/combase/netinfo.c guest/combase/async.c guest/combase/collections.c)
        elseif(name STREQUAL "cryptowinrt")
            list(APPEND sources guest/combase/keycred.c guest/combase/async.c)
        elseif(name STREQUAL "dnsrslvr")
            # the service makes its lookups with dnsapi's code.
            list(APPEND sources guest/dnsapi/dnsclient.c guest/dnsapi/dnswire.c guest/dnsapi/dnsrecord.c)
        endif()
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
        ${services_arch}-lib-crypto:guest/lib/crypto/hmac.c ${services_arch}-lib-crypto:guest/lib/crypto/aes.c
        ${services_arch}-lib-crypto:guest/lib/crypto/modes.c ${services_arch}-lib-crypto:guest/lib/crypto/kdf.c
    LIBS ${MIME_LIB_${services_arch}}/ntdll.lib ${MIME_LIB_${services_arch}}/kernel32.lib
        ${MIME_LIB_${services_arch}}/advapi32.lib ${MIME_LIB_${services_arch}}/rpcrt4.lib
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

# the process of the security authority, which hosts the key isolation service of cng.
mime_objects(${MIME_GUEST_ARCH}-lsass ${MIME_GUEST_ARCH} SOURCES guest/lsass/main.c)
mime_link(${MIME_OUT_${MIME_GUEST_ARCH}}/lsass.exe ${MIME_GUEST_ARCH} OBJECTS ${MIME_GUEST_ARCH}-lsass ${MIME_GUEST_ARCH}-chkstk
    LIBS ${MIME_LIB_${MIME_GUEST_ARCH}}/ntdll.lib ${MIME_LIB_${MIME_GUEST_ARCH}}/kernel32.lib
        ${MIME_LIB_${MIME_GUEST_ARCH}}/advapi32.lib
    FLAGS /subsystem:console,10.0 /entry:entry /base:0x7FF6A7000000 /highentropyva /dynamicbase /stack:0x100000,0x4000
    RESOURCE lsass.exe ROM ${MIME_ROM_${MIME_GUEST_ARCH}}/lsass.exe GROUPS guest)

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

mime_glob(runonce_sources guest/runonce/*.c)
foreach(arch IN LISTS MIME_USER_ARCHS ITEMS x86)
    if(arch STREQUAL "x86" OR arch STREQUAL MIME_GUEST_ARCH)
        mime_objects(${arch}-runonce ${arch} SOURCES ${runonce_sources} INCLUDES guest/runonce)
        set(runonce_flags /subsystem:windows,10.0 /entry:entry /base:0x7FF6A4000000 /highentropyva /dynamicbase
            /stack:0x100000,0x4000)
        if(arch STREQUAL "x86")
            set(runonce_flags /subsystem:windows,6.0 /entry:entry /base:0x00400000 /dynamicbase /stack:0x100000,0x4000)
        endif()
        mime_link(${MIME_OUT_${arch}}/runonce.exe ${arch} OBJECTS ${arch}-runonce ${arch}-chkstk
            LIBS ${MIME_LIB_${arch}}/ntdll.lib ${MIME_LIB_${arch}}/kernel32.lib ${MIME_LIB_${arch}}/advapi32.lib
            FLAGS ${runonce_flags} RESOURCE runonce.exe ROM ${MIME_ROM_${arch}}/runonce.exe GROUPS guest)
    endif()
endforeach()

mime_glob(regsvr32_sources guest/regsvr32/*.c)
foreach(arch IN LISTS MIME_USER_ARCHS ITEMS x86)
    if(arch STREQUAL "x86" OR arch STREQUAL MIME_GUEST_ARCH)
        mime_objects(${arch}-regsvr32 ${arch} SOURCES ${regsvr32_sources} INCLUDES guest/regsvr32)
        set(regsvr32_libs ${MIME_LIB_${arch}}/ntdll.lib ${MIME_LIB_${arch}}/kernel32.lib ${MIME_LIB_${arch}}/user32.lib
            ${MIME_LIB_${arch}}/ole32.lib ${MIME_LIB_${arch}}/kernelbase.lib)
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
            ${MIME_LIB_${arch}}/ole32.lib ${MIME_LIB_${arch}}/kernelbase.lib)
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

# ===================================== explorer =====================================

# explorer.exe, which shows a folder in the file manager of the host and a web address in its browser. it is at
# C:\Windows\explorer.exe and, for the 32 bit one, C:\Windows\SysWOW64\explorer.exe.
foreach(arch ${MIME_GUEST_ARCH} x86)
    set(explorer_rom ${MIME_ROM_${arch}})
    if(arch STREQUAL MIME_GUEST_ARCH)
        set(explorer_rom ${MIME_ROM_WINDOWS})
    endif()
    mime_objects(${arch}-explorer ${arch} SOURCES guest/explorer/main.c)
    set(explorer_libs ${MIME_LIB_${arch}}/ntdll.lib ${MIME_LIB_${arch}}/kernel32.lib ${MIME_LIB_${arch}}/win32u.lib
        ${MIME_LIB_${arch}}/shell32.lib ${MIME_LIB_${arch}}/advapi32.lib ${MIME_LIB_${arch}}/version.lib)
    set(explorer_flags /subsystem:windows,10.0 /entry:entry /base:0x7FF6C7000000 /highentropyva /dynamicbase
        /stack:0x100000,0x4000)
    if(arch STREQUAL "x86")
        set(explorer_flags /subsystem:windows,6.0 /entry:entry /base:0x00400000 /dynamicbase /stack:0x100000,0x4000)
    endif()
    mime_link(${MIME_OUT_${arch}}/explorer.exe ${arch} OBJECTS ${arch}-explorer ${arch}-chkstk LIBS ${explorer_libs}
        FLAGS ${explorer_flags} RESOURCE explorer.exe ROM ${explorer_rom}/explorer.exe GROUPS guest)
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

# microsoft's on every machine, see the genuine section.
foreach(name ntdll kernel32 win32u gdi32 user32 msvcrt ucrtbase bcrypt oleaut32)
    mime_implib(${name} x86)
endforeach()

mime_sources(sources kernelbase x86)
list(APPEND sources guest/ntdll/actgen.c guest/ntdll/manifest.c)
mime_dll(kernelbase x86 BASE 0x76000000 SOURCES ${sources} OBJECTS x86-chkstk OPTIONS -DMIME_BUILD_KERNELBASE
    IMPORTS ntdll)

mime_dll(mscoree x86 BASE 0x03000000 SOURCES guest/mscoree/main.c guest/mscoree/x86/common.S ${gen86}/mscoree_thunks.S OBJECTS x86-chkstk IMPORTS ntdll kernel32)

set(all_five ntdll kernel32 user32 gdi32 win32u)
set(MIME_X86_DLLS
    shlwapi:0x71000000:shlwapi_imports shell32:0x70000000:shell32_imports winmm:0x6f000000:all_five version:0x6e000000:all_five
    advapi32:0x6d000000:advapi32_imports sechost:0x0F000000:base ws2_32:0x6c000000:all_five uxtheme:0x6b000000:all_five
    netapi32:0x6a000000:registry_imports wtsapi32:0x69000000:all_five secur32:0x68000000:secur32_imports
    crypt32:0x67000000:crypt32_imports userenv:0x66000000:registry_imports ole32:0x65000000:ole32_imports
    rpcrt4:0x63000000:rpcrt4_imports imm32:0x62000000:all_five comctl32:0x61000000:comctl comdlg32:0x60000000:dialog_imports
    msi:0x5f000000:msi_imports mpr:0x5e000000:base wintrust:0x5d000000:wintrust_imports odbc32:0x5c000000:user
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
    uiautomationcore:0x1C000000:uia_imports iphlpapi:0x1B000000:base
    mfplat:0x12400000:mfplat_imports mf:0x12200000:mf_imports mfreadwrite:0x12000000:mfreadwrite_imports
    mfcaptureengine:0x12100000:mfce_imports
    schannel:0x19000000:base msctf:0x0E000000:ctf_imports activeds:0x47000000:ads_imports advpack:0x10000000:advpack_imports
    imagehlp:0x02000000:base cabinet:0x09000000:base sfc:0x08000000:base sfc_os:0x07000000:sfc_os_imports
    dnsapi:0x04000000:dnsapi_imports msxml3:0x06000000:msxml_imports msxml6:0x05000000:msxml_imports windowscodecs:0x5A000000:wic_imports
    onecorecommonproxystub:0x02000000:proxy_imports onecoreuapcommonproxystub:0x01000000:proxy_imports
    feclient:0x03000000:base wkscli:0x55600000:wkscli_imports zipfldr:0x15800000:explorerframe_imports mstask:0x31D00000:mstask_imports msado15:0x31C00000:msado15_imports vbscript:0x30900000:vbscript_imports jscript:0x30A00000:jscript_imports scrrun:0x31B00000:scrrun_imports wshom:0x31A00000:wshom_imports avifil32:0x16800000:avifil32_imports glu32:0x16C00000:base dbghelp:0x32D00000:base odbccp32:0x55500000:base mscms:0x55400000:mscms_imports mapi32:0x55300000:mapi32_imports hlink:0x55200000:hlink_imports atl:0x55800000:base authz:0x55100000:authz_imports wldap32:0x55000000:base ntdsapi:0x32C00000:base mlang:0x32E00000:mlang_imports query:0x32B00000:all_five fontsub:0x32A00000:base tdh:0x32900000:base windows.networking:0x32800000:base dbgeng:0x32700000:base wmvcore:0x32600000:base atmlib:0x32500000:base avicap32:0x32400000:all_five rstrtmgr:0x32300000:base wlanapi:0x32200000:base snmpapi:0x32100000:base ktmw32:0x32000000:base httpapi:0x31F00000:base wbemcore:0x31E00000:wbem_imports loadperf:0x1E800000:loadperf_imports xmllite:0x1E700000:xmllite_imports security:0x1E600000:base sspicli:0x1E300000:base rsaenh:0x1E200000:mscms_imports msvfw32:0x1FC00000:msvfw32_imports normaliz:0x1F800000:base cryptsp:0x1F400000:base sensapi:0x55900000:base verifier:0x55A00000:base shfolder:0x1F000000:base msdmo:0x32F00000:mstask_imports dxva2:0x32F80000:dxva2_imports srvcli:0x1E900000:base netutils:0x1EA00000:base)
set(base ntdll kernel32)
set(mscms_imports ntdll kernel32 advapi32)
set(wbem_imports ntdll kernel32 advapi32 ole32 oleaut32)
set(loadperf_imports ntdll kernel32 advapi32)
set(xmllite_imports ntdll kernel32)
set(wkscli_imports ntdll kernel32 advapi32 rpcrt4 msvcrt)
set(dnsapi_imports ntdll kernel32 ws2_32 rpcrt4 msvcrt)
set(oledlg_imports ntdll kernel32 user32 ole32 advapi32)
set(msvfw32_imports ntdll kernel32 user32 gdi32 win32u winmm comdlg32)
set(authz_imports ntdll kernel32 advapi32)
set(mapi32_imports ntdll kernel32 ole32)
set(hlink_imports ntdll kernel32 user32 gdi32 win32u shell32)
set(msado15_imports ntdll kernel32 advapi32 ole32 oleaut32 odbc32)
set(scrrun_imports ntdll kernel32 user32 advapi32 ole32 oleaut32 version mpr shell32)
set(wshom_imports ntdll kernel32 user32 advapi32 ole32 oleaut32 shell32 mpr winspool scrrun)
set(vbscript_imports ntdll kernel32 user32 gdi32 win32u advapi32 ole32 oleaut32 msvcrt)
set(objects_vbscript x86-div)
set(jscript_imports ntdll kernel32 user32 advapi32 ole32 oleaut32 msvcrt ucrtbase)
set(objects_jscript x86-div)
set(mstask_imports ntdll kernel32 advapi32 ole32)
set(objects_mstask x86-div)
set(objects_zipfldr x86-lib-png:guest/lib/png/inflate.c x86-div)
set(avifil32_imports ntdll kernel32 user32 gdi32 win32u ole32 msacm32)
set(objects_avifil32 x86-div)
set(includes_zipfldr guest/lib)
set(rpcrt4_imports ntdll kernel32 user32 gdi32 win32u msvcrt)
set(ole32_imports ntdll kernel32 user32 gdi32 win32u rpcrt4 msvcrt advapi32)
set(shlwapi_imports ntdll kernel32 user32 gdi32 win32u kernelbase advapi32)
set(advapi32_imports ntdll kernel32 user32 gdi32 win32u kernelbase)
set(registry_imports ntdll kernel32 user32 gdi32 win32u advapi32)
set(crypt32_imports ntdll kernel32 user32 gdi32 win32u advapi32 rpcrt4 msvcrt)
set(propsys_imports ntdll kernel32 user32 gdi32 win32u ole32 oleaut32 advapi32 shlwapi combase)
set(mlang_imports ntdll kernel32 oleaut32)
set(secur32_imports ntdll kernel32 user32 gdi32 win32u advapi32)
set(objects_propsys x86-lib-crypto:guest/lib/crypto/hash.c)
set(includes_propsys guest/lib)
set(wic_imports ntdll kernel32 user32 gdi32 win32u ole32)
set(shcore_imports ntdll kernel32 user32 gdi32 win32u ole32 advapi32)
set(objects_shcore x86-lib-winrt)
set(includes_shcore guest/lib)
set(proxy_imports ntdll kernel32 user32 gdi32 win32u ole32 oleaut32 rpcrt4)
set(shell32_imports ntdll kernel32 user32 gdi32 win32u ole32 oleaut32 shlwapi kernelbase)
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
set(dxva2_imports ntdll kernel32 user32 ole32)
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
set(msvbvm60_imports ntdll kernel32 user32 gdi32 ole32 oleaut32 advapi32)
set(crypt ntdll kernel32 crypt32)
set(wintrust_imports ntdll kernel32 crypt32 imagehlp)
set(acm ntdll kernel32 user32 gdi32 winmm)
set(direct3d ntdll kernel32 user32 gdi32 win32u msvcrt)
set(setup ntdll kernel32 cfgmgr32 advapi32 version)
set(objects_setupapi x86-lib-cabinet)
set(xinput ntdll kernel32 user32 setupapi)
set(http ntdll kernel32 user32 ws2_32)
set(wininet_imports ntdll kernel32 user32 ws2_32 kernelbase)
set(urlmon_imports ntdll kernel32 user32 wininet ole32 oleaut32 kernelbase)
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
set(objects_combase x86-lib-crypto:guest/lib/crypto/hash.c)
set(includes_combase guest/lib)
set(objects_crypt32 x86-lib-crypto x86-lib-compound x86-lib-cabinet)
set(objects_ncrypt x86-lib-crypto:guest/lib/crypto/aes.c x86-lib-crypto:guest/lib/crypto/des.c x86-lib-crypto:guest/lib/crypto/hash.c x86-lib-crypto:guest/lib/crypto/hmac.c x86-lib-crypto:guest/lib/crypto/kdf.c x86-lib-crypto:guest/lib/crypto/modes.c)
set(includes_ncrypt guest/lib)
set(objects_secur32 x86-lib-crypto)
set(objects_advapi32 x86-lib-crypto:guest/lib/crypto/hash.c x86-lib-crypto:guest/lib/crypto/stream.c
        x86-lib-crypto:guest/lib/crypto/bignum.c x86-lib-crypto:guest/lib/crypto/rsa.c
        x86-lib-crypto:guest/lib/crypto/aes.c x86-lib-crypto:guest/lib/crypto/des.c x86-lib-crypto:guest/lib/crypto/modes.c x86-lib-crypto:guest/lib/crypto/kdf.c x86-lib-crypto:guest/lib/crypto/hmac.c)
foreach(crypto crypt32 secur32 advapi32)
    set(includes_${crypto} guest/lib)
endforeach()
# the 32-bit advapi32 is mime's and has the providers, so cryptsp only forwards there.
set(objects_cryptsp)
set(libs_cryptsp)
set(objects_winhttp x86-lib-png:guest/lib/png/inflate.c)
set(includes_winhttp guest/lib)
set(objects_wininet x86-lib-png:guest/lib/png/inflate.c)
set(includes_wininet guest/lib)

# the hlsl compiler catches its own faults through msvcrt's _except_handler3.
set(d3dcompiler_imports ${all_five} msvcrt)
list(APPEND MIME_X86_DLLS d3dcompiler_47:0x4F000000:d3dcompiler_imports)
list(APPEND MIME_X86_DLLS wldp:0x31900000:base cryptnet:0x55900000:crypt)
# the windows runtime classes genuine combase loads from SysWOW64, as the 32-bit view of the registry names them.
set(runtime_imports ntdll kernel32 user32 gdi32 win32u combase)
list(APPEND MIME_X86_DLLS windows.media.devices:0x48000000:runtime_imports
    windows.devices.enumeration:0x48400000:runtime_imports windows.networking.connectivity:0x48800000:runtime_imports
    cryptowinrt:0x48C00000:runtime_imports)
foreach(runtime windows.media.devices windows.devices.enumeration windows.networking.connectivity cryptowinrt)
    set(includes_${runtime} guest/lib)
endforeach()
set(devmgr_imports ntdll kernel32 user32 gdi32 win32u advapi32 comctl32 setupapi cfgmgr32 uxtheme)
set(newdev_imports ntdll kernel32 setupapi shell32)
list(APPEND MIME_X86_DLLS winsta:0x10800000:all_five)
list(APPEND MIME_X86_DLLS devmgr:0x10400000:devmgr_imports newdev:0x10C00000:newdev_imports)
set(evr_imports ntdll kernel32 mfplat ole32 gdi32 user32 dxva2 d3d9)
list(APPEND MIME_X86_DLLS evr:0x32F90000:evr_imports)
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

# where the 32-bit cryptsp is microsoft's, the CryptoAPI of mime's 32-bit advapi32 is forwarded there, as the calls of
# windows' advapi32 end there, with the SystemFunction exports windows forwards too. the providers of
# guest/advapi32/crypt.c are not built then.
mime_genuine(cryptsp_x86_genuine x86 cryptsp.dll)
# where the 32-bit sechost is microsoft's, the credential functions of mime's 32-bit advapi32 are forwarded there, as
# the calls of windows' advapi32 end there: sechost asks the security authority, so both widths have one store. the
# manager of guest/advapi32/sechost_credman.c is then for the guests whose sechost is mime's.
mime_genuine(sechost_x86_genuine x86 sechost.dll)
set(sechost_credentials BackupCredentials DeleteA DeleteW EncryptAndMarshalBinaryBlob EnumerateA EnumerateW
    FindBestCredentialA FindBestCredentialW Free GetSessionTypes GetTargetInfoA GetTargetInfoW IsMarshaledCredentialW
    IsProtectedA IsProtectedW MarshalCredentialA MarshalCredentialW ParseUserNameWithType ProfileLoaded ProfileLoadedEx
    ProfileUnloaded ProtectA ProtectEx ProtectW ReadA ReadByTokenHandle ReadDomainCredentialsA ReadDomainCredentialsW
    ReadW RestoreCredentials UnmarshalCredentialA UnmarshalCredentialW UnprotectA UnprotectEx UnprotectW WriteA
    WriteDomainCredentialsA WriteDomainCredentialsW WriteW)
set(def_advapi32)
if(cryptsp_x86_genuine OR sechost_x86_genuine)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${MIME_GEN}/x86/advapi32.def)
    file(STRINGS ${MIME_GEN}/x86/advapi32.def advapi32_lines)
    set(advapi32_def "")
    foreach(line IN LISTS advapi32_lines)
        set(keep TRUE)
        if(cryptsp_x86_genuine AND line MATCHES "^ +Crypt[A-Za-z]+=_Crypt")
            set(keep FALSE)
        elseif(sechost_x86_genuine AND line MATCHES "^ +Cred([A-Za-z]+)=_Cred" AND CMAKE_MATCH_1 IN_LIST sechost_credentials)
            set(keep FALSE)
        endif()
        if(keep)
            string(APPEND advapi32_def "${line}\n")
        endif()
    endforeach()
    if(sechost_x86_genuine)
        foreach(export IN LISTS sechost_credentials)
            string(APPEND advapi32_def "    Cred${export}=sechost.Cred${export}\n")
        endforeach()
    endif()
endif()
if(cryptsp_x86_genuine)
    foreach(export AcquireContextA AcquireContextW ContextAddRef CreateHash Decrypt DeriveKey DestroyHash DestroyKey
            DuplicateHash DuplicateKey Encrypt EnumProviderTypesA EnumProviderTypesW EnumProvidersA EnumProvidersW
            ExportKey GenKey GenRandom GetDefaultProviderA GetDefaultProviderW GetHashParam GetKeyParam GetProvParam
            GetUserKey HashData HashSessionKey ImportKey ReleaseContext SetHashParam SetKeyParam SetProvParam
            SetProviderA SetProviderExA SetProviderExW SetProviderW SignHashA SignHashW VerifySignatureA
            VerifySignatureW)
        string(APPEND advapi32_def "    Crypt${export}=cryptsp.Crypt${export}\n")
    endforeach()
    foreach(number 006 007 008 009 010 011 012 013 014 015 016 018 020 021 022 023 024 025 026 027 030 031 032 033)
        string(APPEND advapi32_def "    SystemFunction${number}=cryptsp.SystemFunction${number}\n")
    endforeach()
    string(APPEND advapi32_def "    SystemFunction035=cryptsp.CheckSignatureInFile\n")
endif()
if(cryptsp_x86_genuine OR sechost_x86_genuine)
    file(CONFIGURE OUTPUT ${MIME_BUILD_GEN}/x86/advapi32.def CONTENT "${advapi32_def}" @ONLY)
    set(def_advapi32 DEF ${MIME_BUILD_GEN}/x86/advapi32.def IMPORTS_DEF ${MIME_GEN}/x86/advapi32.imports.def)
endif()

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
    elseif(name STREQUAL "windows.media.devices")
        set(file Windows.Media.Devices.dll)
        list(APPEND sources guest/combase/mediadevice.c guest/combase/audioendpoint.c)
    elseif(name STREQUAL "windows.devices.enumeration")
        set(file Windows.Devices.Enumeration.dll)
        list(APPEND sources guest/combase/devinfo.c guest/combase/audioendpoint.c)
    elseif(name STREQUAL "windows.networking.connectivity")
        set(file Windows.Networking.Connectivity.dll)
        list(APPEND sources guest/combase/netinfo.c guest/combase/async.c guest/combase/collections.c)
    elseif(name STREQUAL "cryptowinrt")
        set(file CryptoWinRT.dll)
        list(APPEND sources guest/combase/keycred.c guest/combase/async.c)
    endif()
    if(name STREQUAL "advapi32" AND cryptsp_x86_genuine)
        list(FILTER sources EXCLUDE REGEX "/advapi32/crypt\\.c$")
    endif()
    mime_dll(${name} x86 BASE ${address} FILE ${file} SOURCES ${sources} OBJECTS x86-chkstk ${objects_${name}}
        INCLUDES ${includes_${name}} IMPORTS ${${imports}} DEPENDS ${depends_${name}} ${def_${name}})
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
            ${features_arch}-lib-cabinet ${features_arch}-lib-png ${features_arch}-lib-lzms ${features_arch}-lib-wim ${features_arch}-lib-crypto:guest/lib/crypto/hash.c LIBS ${features_libs}
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
        set(cmd_libs ${MIME_LIB_${arch}}/ntdll.lib ${MIME_LIB_${arch}}/kernel32.lib ${MIME_LIB_${arch}}/advapi32.lib)
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

# ===================================== nls =====================================

# the nls files of System32 that the kernel maps for the system dll, written from the tables of gen in the layout of
# windows: a code page file for every page windows has one for, the case tables, the locales and the normalization forms.
add_executable(nlsc host/nlsc/nlsc.c)
target_include_directories(nlsc PRIVATE ${MIME_GEN})
target_compile_options(nlsc PRIVATE -Wall -Wextra)
set_target_properties(nlsc PROPERTIES C_STANDARD 23 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tools)

set(MIME_NLS ${MIME_ASSETS}/nls)
set(nls_files ${MIME_NLS}/l_intl.nls ${MIME_NLS}/locale.nls ${MIME_NLS}/normnfc.nls ${MIME_NLS}/normnfd.nls
    ${MIME_NLS}/normnfkc.nls ${MIME_NLS}/normnfkd.nls ${MIME_NLS}/normidna.nls)
file(STRINGS ${MIME_GEN}/nls_cpfit.h nls_pages REGEX "^    {[0-9]+, [0-9]+, [0-9]+, [0-9]+, [0-9]+},$")
foreach(page IN LISTS nls_pages)
    string(REGEX REPLACE "^    {([0-9]+),.*$" "\\1" page "${page}")
    if(page LESS 100)
        set(page "0${page}")
    endif()
    list(APPEND nls_files ${MIME_NLS}/C_${page}.NLS)
endforeach()
set(nls_tables ${MIME_GEN}/nls_cpdata.h ${MIME_GEN}/nls_cpfit.h ${MIME_GEN}/nls_case.h ${MIME_GEN}/nls_files.h)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${MIME_GEN}/nls_cpfit.h)
add_custom_command(OUTPUT ${nls_files}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${MIME_NLS}
    COMMAND nlsc ${MIME_NLS}
    DEPENDS nlsc ${nls_tables} COMMENT "Writing the nls files" VERBATIM)
set_property(GLOBAL APPEND PROPERTY MIME_ROM_FILES ${nls_files})

# ===================================== fonts =====================================

# the folder's desktop.ini, which gives it the fonts view of the shell as on windows.
set_property(GLOBAL APPEND PROPERTY MIME_ROM_FILES ${CMAKE_SOURCE_DIR}/data/winfonts/desktop.ini)

# ===================================== typelibs =====================================

# The automation type library every other library imports, as Windows ships it: the 64-bit one as
# C:\Windows\System32\stdole2.tlb and the 32-bit one as C:\Windows\SysWOW64\stdole2.tlb.
set_property(GLOBAL APPEND PROPERTY MIME_ROM_FILES ${CMAKE_SOURCE_DIR}/data/wintypelibs/stdole2.tlb
    ${CMAKE_SOURCE_DIR}/data/syswow64/stdole2.tlb)

# ===================================== vulkan =====================================

# the manifest that names vulkan-1.dll as the adapter's vulkan driver, for the khronos loader a program ships: as
# C:\Windows\System32\vulkan-icd.json and C:\Windows\SysWOW64\vulkan-icd.json, each beside the dll it names.
set_property(GLOBAL APPEND PROPERTY MIME_ROM_FILES ${CMAKE_SOURCE_DIR}/data/vulkan/vulkan-icd.json
    ${CMAKE_SOURCE_DIR}/data/syswow64/vulkan-icd.json)

# ===================================== color =====================================

# the sRGB profile of the color folder, made by scripts/asset/icc.cmake, served from the ROM as
# C:\Windows\System32\spool\drivers\color\sRGB Color Space Profile.icm.
set(mime_srgb_profile "${MIME_ASSETS}/wincolor/sRGB Color Space Profile.icm")
add_custom_command(OUTPUT "${mime_srgb_profile}"
    COMMAND ${CMAKE_COMMAND} "-DOUTPUT=${mime_srgb_profile}" -P ${CMAKE_SOURCE_DIR}/scripts/asset/icc.cmake
    DEPENDS ${CMAKE_SOURCE_DIR}/scripts/asset/icc.cmake ${CMAKE_SOURCE_DIR}/scripts/asset/binary.cmake VERBATIM)
set_property(GLOBAL APPEND PROPERTY MIME_ROM_FILES "${mime_srgb_profile}")

# ===================================== theme =====================================

# the visual style of the machine, committed in gen/themes and carried in the
# ROM as Windows\Resources\Themes\aero\aero.msstyles, see scripts/rom.cmake.
set_property(GLOBAL APPEND PROPERTY MIME_ROM_FILES ${CMAKE_SOURCE_DIR}/gen/themes/aero/aero.msstyles)

# ===================================== host =====================================

get_property(rom_files GLOBAL PROPERTY MIME_ROM_FILES)
# the files of the genuine manifest are named in the rom with no bytes, see the genuine section and scripts/rom.cmake.
set(rom_sysx64 OFF)
if(MIME_GUEST_ARCH STREQUAL "arm64")
    set(rom_sysx64 ON)
endif()
add_custom_command(OUTPUT ${MIME_BUILD_GEN}/rom.S
    COMMAND ${CMAKE_COMMAND} -DOUTPUT=${MIME_BUILD_GEN}/rom.S -DSYSTEM=${CMAKE_SYSTEM_NAME} "-DFILES=${rom_files}"
        -DMANIFEST=${genuine_manifest} -DSYSX64=${rom_sysx64} -P ${CMAKE_SOURCE_DIR}/scripts/rom.cmake
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
    pkg_check_modules(HOST_LIBS REQUIRED IMPORTED_TARGET wayland-client wayland-cursor xkbcommon)
    pkg_check_modules(HOST_SDBUS IMPORTED_TARGET libsystemd)
    find_package(Threads REQUIRED)
    mime_glob(host_sources host/*.c host/linux/*.c host/vmm/*.c host/camera/*.c guest/lib/png/*.c)
    list(FILTER host_sources EXCLUDE REGEX "/host/vmm/(vm|vcpu|lazy|loader|window)[.]c$")
    list(APPEND host_sources host/gpu/shader.c host/gpu/glsl_compat.c host/ui/pe_icon.c)

    # the protocols of the compositor past the core one, from the descriptions kept in third_party/wayland-protocols.
    find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)
    set(wayland_gen ${MIME_BUILD_GEN}/wayland)
    file(MAKE_DIRECTORY ${wayland_gen})
    foreach(protocol xdg-shell viewporter tablet-v2 fractional-scale-v1 cursor-shape-v1 xdg-toplevel-icon-v1
            text-input-unstable-v3 pointer-constraints-unstable-v1 relative-pointer-unstable-v1 xdg-output-unstable-v1)
        set(xml ${CMAKE_SOURCE_DIR}/third_party/wayland-protocols/${protocol}.xml)
        add_custom_command(OUTPUT ${wayland_gen}/${protocol}.h ${wayland_gen}/${protocol}.c
            COMMAND ${WAYLAND_SCANNER} client-header ${xml} ${wayland_gen}/${protocol}.h
            COMMAND ${WAYLAND_SCANNER} private-code ${xml} ${wayland_gen}/${protocol}.c
            DEPENDS ${xml} VERBATIM)
        list(APPEND host_sources ${wayland_gen}/${protocol}.c)
        list(APPEND wayland_headers ${wayland_gen}/${protocol}.h)
    endforeach()

    add_executable(mime ${host_sources} ${vulkan_host_sources} ${MIME_BUILD_GEN}/rom.S $<TARGET_OBJECTS:mvk-host>
        $<TARGET_OBJECTS:shaderc>)
    set_source_files_properties(host/gpu/shader.c PROPERTIES
        INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/third_party;${CMAKE_SOURCE_DIR}/third_party/glslang")
    set_source_files_properties(host/linux/gpu.c PROPERTIES COMPILE_OPTIONS -Wno-missing-field-initializers
        INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/third_party/khronos;${vulkan_gen};${CMAKE_SOURCE_DIR}/third_party/glslang")
    set_source_files_properties(host/linux/wayland.c PROPERTIES OBJECT_DEPENDS "${wayland_headers}")
    target_include_directories(mime PRIVATE ${wayland_gen} guest/lib)
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
    # only the headers: the library is loaded at run time, so a host without PipeWire still starts, with no sound.
    pkg_check_modules(HOST_PIPEWIRE REQUIRED libpipewire-0.3)
    set(pipewire_includes "")
    foreach(dir ${HOST_PIPEWIRE_INCLUDE_DIRS})
        list(APPEND pipewire_includes "-isystem${dir}")
    endforeach()
    set_source_files_properties(host/linux/pipewire.c PROPERTIES COMPILE_OPTIONS "${pipewire_includes}")
    target_link_libraries(mime PRIVATE ${CMAKE_DL_LIBS})
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
# the reader of the windows image, the same as features.exe's, for the files of the manifest that come from it.
target_sources(mime PRIVATE ${MIME_BUILD_GEN}/genuine_manifest.h guest/lib/wim/wim.c guest/lib/lzms/lzms.c)
set_source_files_properties(host/genuine.c PROPERTIES INCLUDE_DIRECTORIES ${MIME_BUILD_GEN}
    OBJECT_DEPENDS ${MIME_BUILD_GEN}/genuine_manifest.h)
find_package(CURL REQUIRED)
# the sdk's usr/include as -isystem comes before libc++'s headers and breaks them, and the compiler searches it already.
if(CURL_INCLUDE_DIR MATCHES "\\.sdk/usr/include$")
    set_target_properties(CURL::libcurl PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "")
endif()
target_link_libraries(mime PRIVATE CURL::libcurl)
