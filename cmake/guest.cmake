# the guest is windows code built by the same clang for other targets, linked by lld-link. cmake compiles it
# as object libraries, so every source is in compile_commands.json, and custom commands link the pe files.

set(MIME_TRIPLE_x64 --target=x86_64-pc-windows-msvc -march=x86-64 -mcx16)
set(MIME_TRIPLE_arm64 --target=aarch64-pc-windows-msvc -march=armv8.2-a)
set(MIME_TRIPLE_x86 --target=i686-pc-windows-msvc -march=pentium4)
set(MIME_MACHINE_x64 x64)
set(MIME_MACHINE_arm64 arm64)
set(MIME_MACHINE_x86 x86)

set(MIME_GUEST_C_OPTIONS -ffreestanding -nostdlibinc -fms-extensions -fno-stack-protector -gdwarf
    -fmacro-prefix-map=${CMAKE_SOURCE_DIR}/=
    -Wall -Wextra -Wimplicit-fallthrough -Wno-unused-parameter -Wno-missing-field-initializers)
# the c++ of the c runtime's own c++ abi: microsoft's, with exceptions and type information.
set(MIME_GUEST_CXX_OPTIONS -fno-threadsafe-statics -fexceptions -fcxx-exceptions)

# a flavor is a processor and the flags its code shares: kernel is the machine's own processor for
# ntoskrnl.exe and win32k.sys, arm64, x64 and x86 the user mode dlls of each.
function(mime_flavor name arch)
    add_library(mime-flags-${name} INTERFACE)
    target_compile_options(mime-flags-${name} INTERFACE ${MIME_TRIPLE_${arch}}
        "$<$<COMPILE_LANGUAGE:C,CXX>:${MIME_GUEST_C_OPTIONS};${ARGN}>"
        "$<$<COMPILE_LANGUAGE:CXX>:${MIME_GUEST_CXX_OPTIONS}>")
    target_include_directories(mime-flags-${name} INTERFACE include guest/include guest/rtl ${MIME_GEN})
    set(MIME_ARCH_${name} ${arch} PARENT_SCOPE)
endfunction()

mime_flavor(kernel ${MIME_GUEST_ARCH})
mime_flavor(arm64 arm64)
# -ffreestanding drops the x64 unwind tables, which exception unwinding walks through every dll.
if(MIME_GUEST_ARCH STREQUAL "x64")
    # on an x64 machine these are the system's own dlls, run natively, not by the translator.
    mime_flavor(x64 x64 -fasynchronous-unwind-tables -DMIME_X64_NATIVE=1)
else()
    mime_flavor(x64 x64 -fasynchronous-unwind-tables)
endif()
mime_flavor(x86 x86)

# mime_objects(<target> <flavor> SOURCES <files>... [OPTIONS <c flags>...] [INCLUDES <dirs>...] [DEPENDS <targets>...])
# compiles guest sources. OPTIONS apply to c and c++ only, not to assembly.
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
    add_dependencies(${target} mime-gen-headers ${depends})
endfunction()

# the objects of each spec: an object library, or <library>:<source> for the object of one of its sources.
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

# the object libraries of the specs.
function(mime_object_targets out)
    list(TRANSFORM ARGN REPLACE ":.*$" "")
    list(REMOVE_DUPLICATES ARGN)
    set(${out} ${ARGN} PARENT_SCOPE)
endfunction()

# the targets among the items.
function(mime_only_targets out)
    set(list)
    foreach(item IN LISTS ARGN)
        if(TARGET ${item})
            list(APPEND list ${item})
        endif()
    endforeach()
    set(${out} ${list} PARENT_SCOPE)
endfunction()

# the version resource of system file <name> for a processor, from tools/genversion.py and the table of
# tools/fileversions.json, followed by the resources of the script <extra> when given.
function(mime_resource name arch)
    get_property(done GLOBAL PROPERTY MIME_RESOURCE_${arch}_${name})
    if(done)
        return()
    endif()
    set_property(GLOBAL PROPERTY MIME_RESOURCE_${arch}_${name} TRUE)
    set(extra ${ARGN})
    set(base ${MIME_GEN}/res/${MIME_MACHINE_${arch}}/${name})
    add_custom_command(OUTPUT ${base}.res
        COMMAND ${CMAKE_COMMAND} -E make_directory ${MIME_GEN}/res/${MIME_MACHINE_${arch}}
        COMMAND Python3::Interpreter -I ${CMAKE_SOURCE_DIR}/tools/genversion.py ${CMAKE_SOURCE_DIR}/tools/fileversions.json
            ${name} ${arch} ${base}.rc ${extra}
        COMMAND ${MIME_LLVM_RC} -no-preprocess -c 1252 -fo ${base}.res ${base}.rc
        DEPENDS tools/genversion.py tools/fileversions.json ${extra} VERBATIM)
endfunction()

set(MIME_LINK_FLAGS /nologo /debug:dwarf /nodefaultlib /opt:ref /incremental:no /filealign:0x1000 /version:10.0
    /osversion:10.0 /Brepro /release)

# mime_link(<output> <arch> OBJECTS <specs>... [LIBS <files>...] [FLAGS <lld-link flags>...] [DEPENDS <files>...]
#           [IMPLIB <lib>] [RESOURCE <file name>] [RESOURCE_SCRIPT <rc>] [IMPORTS <imports.def>]
#           [GROUPS <groups>...])
# links a pe file. IMPLIB has lld-link write the import library, IMPORTS has llvm-dlltool make it from a
# definition of stdcall names instead, as x86 import libraries keep the decoration the dll drops.
function(mime_link output arch)
    cmake_parse_arguments(PARSE_ARGV 2 arg "" "IMPLIB;RESOURCE;RESOURCE_SCRIPT;IMPORTS" "OBJECTS;LIBS;FLAGS;DEPENDS;GROUPS")
    mime_object_list(objects ${arg_OBJECTS})
    mime_object_targets(object_targets ${arg_OBJECTS})
    set(flags /machine:${MIME_MACHINE_${arch}} ${MIME_LINK_FLAGS} ${arg_FLAGS})
    if(arch STREQUAL "x86")
        list(APPEND flags /safeseh:no)
    endif()
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
        set(resource ${MIME_GEN}/res/${MIME_MACHINE_${arch}}/${arg_RESOURCE}.res)
    endif()
    get_filename_component(directory ${output} DIRECTORY)
    file(RELATIVE_PATH comment ${CMAKE_BINARY_DIR} ${output})
    add_custom_command(OUTPUT ${outputs}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${directory}
        COMMAND ${MIME_LLD_LINK} ${flags} /out:${output} ${objects} ${arg_LIBS} ${resource}
        ${post}
        DEPENDS ${objects} ${object_targets} ${arg_LIBS} ${resource} ${arg_DEPENDS}
        COMMENT "Linking ${comment}" COMMAND_EXPAND_LISTS VERBATIM)
    foreach(group IN LISTS arg_GROUPS)
        set_property(GLOBAL APPEND PROPERTY MIME_GROUP_${group} ${output})
    endforeach()
endfunction()

# the copy of a pe file embedded in mime, without the debug information that stays in guest/ for symbolizing.
function(mime_rom_file source destination)
    get_filename_component(directory ${destination} DIRECTORY)
    add_custom_command(OUTPUT ${destination}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${directory}
        COMMAND ${MIME_LLVM_STRIP} --strip-debug ${source} -o ${destination}
        COMMAND $<TARGET_FILE:pechecksum> ${destination}
        DEPENDS ${source} pechecksum VERBATIM)
    set_property(GLOBAL APPEND PROPERTY MIME_ROM_FILES ${destination})
endfunction()

# the custom target <name> building the files of group <name>.
function(mime_group name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "ALL" "" "DEPENDS")
    get_property(files GLOBAL PROPERTY MIME_GROUP_${name})
    set(all)
    if(arg_ALL)
        set(all ALL)
    endif()
    add_custom_target(${name} ${all} DEPENDS ${files} ${arg_DEPENDS})
endfunction()

# the files matching each glob pattern, relative to the source directory, sorted pattern by pattern.
function(mime_glob out)
    set(files)
    foreach(pattern IN LISTS ARGN)
        file(GLOB found CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/${pattern})
        list(SORT found)
        list(APPEND files ${found})
    endforeach()
    set(${out} ${files} PARENT_SCOPE)
endfunction()
