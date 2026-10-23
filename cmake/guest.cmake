# guest code is compiled as object libraries, so every source shows up in compile_commands.json, then linked by
# lld-link from custom commands.

set(MIME_TRIPLE_x64 --target=x86_64-pc-windows-msvc -march=x86-64 -mcx16)
set(MIME_TRIPLE_arm64 --target=aarch64-pc-windows-msvc -march=armv8.2-a)
set(MIME_TRIPLE_x86 --target=i686-pc-windows-msvc -march=pentium4)
set(MIME_MACHINE_x64 x64)
set(MIME_MACHINE_arm64 arm64)
set(MIME_MACHINE_x86 x86)

set(MIME_GUEST_C_OPTIONS -ffreestanding -nostdlibinc -fms-extensions -fno-stack-protector -gdwarf
    -fmacro-prefix-map=${CMAKE_SOURCE_DIR}/=
    -Wall -Wextra -Wimplicit-fallthrough -Wno-unused-parameter -Wno-missing-field-initializers)
# the crt implements the Microsoft c++ abi, exceptions and rtti included.
set(MIME_GUEST_CXX_OPTIONS -fno-threadsafe-statics -fexceptions -fcxx-exceptions)

# a flavor is a target processor plus shared flags. kernel builds ntoskrnl.exe and win32k.sys for the guest
# processor, arm64, x64 and x86 build the user mode dlls.
function(mime_flavor name arch)
    add_library(mime-flags-${name} INTERFACE)
    target_compile_options(mime-flags-${name} INTERFACE ${MIME_TRIPLE_${arch}}
        "$<$<COMPILE_LANGUAGE:C,CXX>:${MIME_GUEST_C_OPTIONS};${ARGN}>"
        "$<$<COMPILE_LANGUAGE:CXX>:${MIME_GUEST_CXX_OPTIONS}>")
    target_include_directories(mime-flags-${name} INTERFACE include guest/include guest/rtl ${MIME_GEN})
    set(MIME_ARCH_${name} ${arch} PARENT_SCOPE)
endfunction()

mime_flavor(kernel ${MIME_GUEST_ARCH})
# -ffreestanding drops unwind tables, but exceptions unwind through every dll.
mime_flavor(arm64 arm64 -fasynchronous-unwind-tables)
if(MIME_GUEST_ARCH STREQUAL "x64")
    # on an x64 host these run natively instead of through the translator.
    mime_flavor(x64 x64 -fasynchronous-unwind-tables -DMIME_X64_NATIVE=1)
else()
    mime_flavor(x64 x64 -fasynchronous-unwind-tables)
endif()
mime_flavor(x86 x86)

# mime_objects(<target> <flavor> SOURCES <files>... [OPTIONS <c flags>...] [INCLUDES <dirs>...] [DEPENDS <targets>...])
# OPTIONS apply to c and c++, not to assembly.
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

# a spec is an object library, or <library>:<source> for a single object of it.
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

# the version resource of <name>, followed by the resource script given after arch. x86 has a version of its own
# only where it differs.
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
    add_custom_command(OUTPUT ${base}.res
        COMMAND ${CMAKE_COMMAND} -E make_directory ${MIME_BUILD_GEN}/res/${MIME_MACHINE_${arch}}
        COMMAND ${CMAKE_COMMAND} -DOUTPUT=${base}.rc "-DINPUTS=${inputs}" -P ${CMAKE_SOURCE_DIR}/cmake/concat.cmake
        COMMAND ${MIME_LLVM_RC} -no-preprocess -c 1252 -fo ${base}.res -- ${base}.rc
        DEPENDS ${inputs} cmake/concat.cmake VERBATIM)
endfunction()

# /build-id gives the links with and without debug information the same debug directory, so the same layout.
set(MIME_LINK_FLAGS /nologo /debug:dwarf /build-id /nodefaultlib /opt:ref /incremental:no /filealign:0x1000
    /version:10.0 /osversion:10.0 /Brepro /release)

# mime_link(<output> <arch> OBJECTS <specs>... [LIBS <files>...] [FLAGS <lld-link flags>...] [DEPENDS <files>...]
#           [IMPLIB <lib>] [RESOURCE <file name>] [RESOURCE_SCRIPT <rc>] [IMPORTS <imports.def>]
#           [ROM <file>] [GROUPS <groups>...])
# links a pe file. IMPLIB has lld-link write the import library, IMPORTS has llvm-dlltool make it from a
# definition of stdcall names instead, as x86 import libraries keep the decoration the dll drops.
# ROM links it again without debug information for the rom, at the same addresses as the copy in guest/ that
# symbolizes them.
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

# target <name> builds the files of group <name>.
function(mime_group name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "ALL" "" "DEPENDS")
    get_property(files GLOBAL PROPERTY MIME_GROUP_${name})
    set(all)
    if(arg_ALL)
        set(all ALL)
    endif()
    add_custom_target(${name} ${all} DEPENDS ${files} ${arg_DEPENDS})
endfunction()

# globs relative to the source directory, sorted per pattern so the link order stays stable.
function(mime_glob out)
    set(files)
    foreach(pattern IN LISTS ARGN)
        file(GLOB found CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/${pattern})
        list(SORT found)
        list(APPEND files ${found})
    endforeach()
    set(${out} ${files} PARENT_SCOPE)
endfunction()
