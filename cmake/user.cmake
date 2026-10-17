# the user mode dlls: arm64 ones into System32 on an arm64 machine, x64 ones into SysX64 there or
# System32 on an x64 machine, and x86 ones into SysWOW64.

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
    set(MIME_NATIVE_x86_REF ${MIME_SYSX64})
else()
    set(MIME_ROM_x64 ${MIME_ROM}/sysx64)
    set(MIME_NATIVE_x86_REF ${MIME_SYSTEM32})
endif()
set(MIME_ROM_x86 ${MIME_ROM}/syswow64)
set(MIME_DLL_FLAGS_arm64 /dll /subsystem:console,10.0 /highentropyva /dynamicbase /stack:0x40000,0x1000)
set(MIME_DLL_FLAGS_x64 ${MIME_DLL_FLAGS_arm64})
set(MIME_DLL_FLAGS_x86 /dll /subsystem:console,6.0 /dynamicbase /stack:0x40000,0x1000)

# the runtime every flavor links: the rtl, the crt without builtins and the stack probe.
set(MIME_CHKSTK_arm64 guest/crt/chkstk.S)
set(MIME_CHKSTK_x64 guest/crt/x64/chkstk.S)
set(MIME_CHKSTK_x86 guest/crt/x86/chkstk.S)
foreach(arch IN LISTS MIME_USER_ARCHS ITEMS x86)
    mime_objects(${arch}-rtl ${arch} SOURCES ${rtl_sources})
    mime_objects(${arch}-crt ${arch} SOURCES ${crt_sources} OPTIONS -fno-builtin)
    mime_objects(${arch}-chkstk ${arch} SOURCES ${MIME_CHKSTK_${arch}})
    set(lib_includes guest/lib third_party/SPIRV-Cross)
    foreach(lib privdata dxbc dxil png msstyles compound audio)
        mime_glob(lib_sources guest/lib/${lib}/*.c)
        mime_objects(${arch}-lib-${lib} ${arch} SOURCES ${lib_sources} INCLUDES ${lib_includes})
    endforeach()
endforeach()
mime_objects(x86-div x86 SOURCES guest/crt/x86/div.c guest/crt/x86/div_entry.S)
set(MIME_RUNTIME_arm64 arm64-rtl arm64-crt arm64-chkstk)
set(MIME_RUNTIME_x64 x64-rtl x64-crt x64-chkstk)
set(MIME_RUNTIME_x86 x86-rtl x86-crt x86-chkstk x86-div)

# mime_dll(<name> <arch> BASE <address> [FILE <file name>] [ENTRY <flag>] [SOURCES <files>...] [OBJECTS <specs>...]
#          [OPTIONS <flags>...] [INCLUDES <dirs>...] [IMPORTS <dlls>...] [LIBS <files>...] [FLAGS <flags>...]
#          [DEF <file>] [IMPORTS_DEF <file>] [DEPENDS <targets or files>...] [NOIMPLIB])
# a dll of guest/<name>, its sources compiled with -Iguest/<name>, linked against the import libraries of
# IMPORTS, then stripped into the rom. an x86 dll without DEF exports what the native build of it does.
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
    if(arch STREQUAL "x86")
        set(def ${arg_DEF})
        set(imports_def ${arg_IMPORTS_DEF})
        if(NOT def)
            set(def ${MIME_GEN}/x86/${name}.def)
            set(imports_def ${MIME_GEN}/x86/${name}.imports.def)
            mime_object_list(object_files ${objects})
            mime_object_targets(object_targets ${objects})
            set(reference ${MIME_NATIVE_x86_REF}/${arg_FILE})
            set(library ${name})
            if(NOT arg_FILE STREQUAL "${name}.dll")
                set(library ${arg_FILE})
            endif()
            set(own_def)
            if(EXISTS ${CMAKE_SOURCE_DIR}/guest/${name}/${name}.def)
                set(own_def guest/${name}/${name}.def)
            endif()
            add_custom_command(OUTPUT ${def} ${imports_def}
                COMMAND Python3::Interpreter tools/genx86exports.py ${library} ${MIME_GEN}/x86/${name} ${reference}
                    ${object_files}
                DEPENDS tools/genx86exports.py ${reference} ${object_files} ${object_targets} ${libs} ${own_def}
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} COMMAND_EXPAND_LISTS VERBATIM)
        endif()
        if(arg_NOIMPLIB)
            mime_link(${output} x86 OBJECTS ${objects} LIBS ${libs} FLAGS ${flags} /def:${def} /noimplib
                DEPENDS ${def} ${depends} GROUPS guest)
        else()
            mime_link(${output} x86 OBJECTS ${objects} LIBS ${libs} FLAGS ${flags} /def:${def}
                DEPENDS ${def} ${imports_def} ${depends} IMPLIB ${implib} IMPORTS ${imports_def} GROUPS guest)
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
                RESOURCE ${arg_FILE} GROUPS guest)
        else()
            mime_link(${output} ${arch} OBJECTS ${objects} LIBS ${libs} FLAGS ${flags} DEPENDS ${depends}
                IMPLIB ${implib} RESOURCE ${arg_FILE} GROUPS guest)
        endif()
    endif()
    mime_rom_file(${output} ${MIME_ROM_${arch}}/${arg_FILE})
endfunction()

# the c files of guest/<name> and the assembly of guest/<name>/<arch>, as each processor builds them.
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

set(nt_export_code "^(Rtl|Ldr|Dbg|vDbg|Etw|Tp|Nls|Csr|Wer)[A-Z]|^KiUser(Exception|Apc|Callback)Dispatcher$|^LdrInitializeThunk$|^VerSetConditionMask$|^(_?str|_?wcs|mem|_vsn|_sn|sprintf|swprintf|to|tow|__chkstk|_i64tow|_itow|_ultow|_wtoi|atoi|_wcs|_str)")
set(MIME_SYSCALLS_arm64 ${MIME_GEN}/ntdll_syscalls.S)
set(MIME_SYSCALLS_x64 ${MIME_GEN}/ntdll_syscalls_x64.S)
set(MIME_WIN32K_SYSCALLS_arm64 ${MIME_GEN}/win32u_syscalls.S)
set(MIME_WIN32K_SYSCALLS_x64 ${MIME_GEN}/win32u_syscalls_x64.S)
set(nt_export_code_arm64 "${nt_export_code}")
set(nt_export_code_x64 "${nt_export_code}|^__C_specific_handler$")

add_custom_command(OUTPUT ${MIME_GEN}/win32u.def
    COMMAND Python3::Interpreter -c
        "import sys; open(sys.argv[2], 'w').write('LIBRARY win32u.dll\\nEXPORTS\\n' + open(sys.argv[1]).read())"
        ${MIME_GEN}/win32u_syscalls.def ${MIME_GEN}/win32u.def
    DEPENDS ${MIME_GEN}/win32u_syscalls.def VERBATIM)

# ntdll, win32u, kernelbase, kernel32, gdi32 and user32 of each native processor.
foreach(arch IN LISTS MIME_USER_ARCHS)
    mime_objects(${arch}-ntsyscalls ${arch} SOURCES ${MIME_SYSCALLS_${arch}})
    mime_objects(${arch}-win32ksyscalls ${arch} SOURCES ${MIME_WIN32K_SYSCALLS_${arch}})

    if(arch STREQUAL "x64")
        mime_glob(ntdll_sources guest/ntdll/*.c guest/ntdll/x64/*.c guest/ntdll/x64/*.S)
    else()
        mime_glob(ntdll_sources guest/ntdll/*.c guest/ntdll/*.S)
    endif()
    mime_objects(${arch}-ntdll ${arch} SOURCES ${ntdll_sources} INCLUDES guest/ntdll)
    set(ntdll_objects ${arch}-ntdll ${arch}-ntsyscalls ${MIME_RUNTIME_${arch}})
    mime_object_list(ntdll_files ${ntdll_objects})
    add_custom_command(OUTPUT ${MIME_GEN}/${arch}/ntdll.def
        COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/tools/gendef.py symbols ntdll.dll ${MIME_GEN}/${arch}/ntdll.def
            --code "${nt_export_code_${arch}}" --prepend ${MIME_GEN}/ntdll_syscalls.def ${ntdll_files}
        DEPENDS tools/gendef.py ${MIME_GEN}/ntdll_syscalls.def ${ntdll_files} ${ntdll_objects}
        COMMAND_EXPAND_LISTS VERBATIM)
    mime_dll(ntdll ${arch} BASE 0x7FFC00000000 ENTRY /noentry OBJECTS ${ntdll_objects}
        DEF ${MIME_GEN}/${arch}/ntdll.def DEPENDS ${MIME_GEN}/${arch}/ntdll.def)

    mime_dll(win32u ${arch} BASE 0x7FF700000000 ENTRY /noentry OBJECTS ${arch}-win32ksyscalls
        FLAGS /subsystem:windows,10.0 DEF ${MIME_GEN}/win32u.def)

    mime_sources(sources kernelbase ${arch})
    mime_dll(kernelbase ${arch} BASE 0x7FFB00000000 SOURCES ${sources} OBJECTS ${arch}-chkstk
        OPTIONS -DMIME_BUILD_KERNELBASE IMPORTS ntdll)

    # kernel32 forwards to kernelbase like windows' does, and a few functions to ntdll, x64 the unwinding ones too.
    set(kernel32_ntdll RtlPcToFileHeader VerSetConditionMask)
    if(arch STREQUAL "x64")
        list(PREPEND kernel32_ntdll RtlCaptureContext RtlLookupFunctionEntry RtlUnwindEx RtlVirtualUnwind
            RtlAddFunctionTable RtlDeleteFunctionTable RtlInstallFunctionTableCallback RtlRestoreContext RtlUnwind
            __C_specific_handler RtlCaptureStackBackTrace)
    endif()
    list(TRANSFORM kernel32_ntdll REPLACE "^(.+)$" "\\1=ntdll.\\1")
    add_custom_command(OUTPUT ${MIME_GEN}/${arch}/kernel32.def
        COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/tools/gendef.py forward kernel32.dll ${MIME_GEN}/${arch}/kernel32.def
            ${MIME_OUT_${arch}}/kernelbase.dll kernelbase ${kernel32_ntdll}
        DEPENDS tools/gendef.py ${MIME_OUT_${arch}}/kernelbase.dll VERBATIM)
    mime_sources(sources kernel32 ${arch})
    mime_dll(kernel32 ${arch} BASE 0x7FFA00000000 SOURCES ${sources} OBJECTS ${arch}-chkstk
        OPTIONS -DMIME_BUILD_KERNEL32 IMPORTS ntdll kernelbase DEF ${MIME_GEN}/${arch}/kernel32.def)

    mime_sources(sources gdi32 ${arch})
    mime_dll(gdi32 ${arch} BASE 0x7FF800000000 SOURCES ${sources} OBJECTS ${arch}-chkstk
        OPTIONS -DMIME_BUILD_GDI32 IMPORTS ntdll kernel32 win32u)

    # user32 parses the visual style and decodes png icons itself.
    mime_sources(sources user32 ${arch})
    mime_dll(user32 ${arch} BASE 0x7FF900000000 SOURCES ${sources}
        OBJECTS ${arch}-chkstk ${arch}-lib-png ${arch}-lib-msstyles OPTIONS -DMIME_BUILD_USER32 INCLUDES guest/lib
        IMPORTS ntdll kernel32 win32u gdi32 FLAGS /subsystem:windows,10.0)
endforeach()

# the system dll of x64 processes on an arm64 machine: the x86 and x64 translator and its native side,
# without imports.
if(MIME_GUEST_ARCH STREQUAL "arm64")
    mime_glob(sources guest/mimex86/*.c guest/mimex86/*.S)
    mime_dll(mimex86 arm64 BASE 0x7FFD00000000 ENTRY /noentry SOURCES ${sources}
        OBJECTS arm64-ntsyscalls ${MIME_RUNTIME_arm64} OPTIONS -fno-math-errno
        FLAGS /export:RtlUserThreadStart=MxUserThreadStart DEPENDS mime-gen-win32k-calls NOIMPLIB)
endif()

# the other dlls, each with the preferred base of windows' layout, importing from the core dlls only and
# reaching each other with LoadLibrary.
set(MIME_EXTRA_DLLS msvcrt:0x7FF6F0000000 advapi32:0x7FF6E0000000 ws2_32:0x7FF6D0000000 iphlpapi:0x7FF6C0000000
    dnsapi:0x7FF6B0000000 ole32:0x7FF6A0000000 oleaut32:0x7FF690000000 rpcrt4:0x7FF680000000
    shell32:0x7FF670000000 shlwapi:0x7FF660000000 comdlg32:0x7FF650000000 comctl32:0x7FF640000000
    crypt32:0x7FF630000000 schannel:0x7FF620000000 secur32:0x7FF610000000 uxtheme:0x7FF600000000
    imm32:0x7FF5F0000000 usp10:0x7FF5E0000000 dwmapi:0x7FF5D0000000 winmm:0x7FF5C0000000
    version:0x7FF5B0000000 userenv:0x7FF5A0000000 wtsapi32:0x7FF590000000 netapi32:0x7FF580000000
    odbc32:0x7FF570000000 winspool:0x7FF560000000 msi:0x7FF550000000 dbghelp:0x7FF540000000
    bcrypt:0x7FF530000000 avrt:0x7FF520000000 dinput8:0x7FF510000000 dwrite:0x7FF500000000
    dxgi:0x7FF4F0000000 wsock32:0x7FF4E0000000 ucrtbase:0x7FF4D0000000 mpr:0x7FF4C0000000 wintrust:0x7FF4B0000000
    mmdevapi:0x7FF4A0000000 gdiplus:0x7FF490000000 wininet:0x7FF480000000 urlmon:0x7FF470000000
    msimg32:0x7FF460000000 oleacc:0x7FF450000000 oledlg:0x7FF440000000 combase:0x7FF430000000
    d3d11:0x7FF420000000 msvcp_win:0x7FF410000000 d3dcompiler_47:0x7FF400000000 d3d12:0x7FF3E0000000
    d3d9:0x7FF3D0000000 setupapi:0x7FF3C0000000 d3d10core:0x7FF3B0000000 d3d10_1core:0x7FF3A0000000
    d3d10:0x7FF390000000 d3d10_1:0x7FF380000000 msacm32:0x7FF370000000 dsound:0x7FF320000000)

foreach(arch IN LISTS MIME_USER_ARCHS)
    # what some take from guest/lib or from another dll's sources.
    set(objects_msi ${arch}-lib-compound)
    set(objects_winmm ${arch}-lib-audio)
    set(objects_dsound ${arch}-lib-audio)
    set(objects_gdiplus ${arch}-lib-png)
    set(objects_dxgi ${arch}-lib-privdata)
    set(objects_d3d11 ${arch}-lib-privdata ${arch}-lib-dxbc)
    # d3d12 compiles dxbc as d3d11 does, dxil too, and shares its table of formats.
    set(objects_d3d12 ${arch}-lib-privdata ${arch}-lib-dxbc ${arch}-lib-dxil ${arch}-d3d11:guest/d3d11/format.c)
    # d3dcompiler_47 reads containers and instructions with the dxbc library, without its compiler.
    set(objects_d3dcompiler_47 ${arch}-lib-dxbc:guest/lib/dxbc/container.c ${arch}-lib-dxbc:guest/lib/dxbc/reader.c)
    # d3d9 writes dxbc containers, checksummed as d3d11 reads them.
    set(objects_d3d9 ${arch}-lib-privdata ${arch}-lib-dxbc:guest/lib/dxbc/container.c)
    set(objects_d3d10 ${arch}-lib-dxbc:guest/lib/dxbc/container.c)
    # d3d10_1 makes devices as d3d10 does, d3d10_1core is d3d10core but for the device it does not make.
    set(objects_d3d10_1 ${arch}-d3d10:guest/d3d10/create.c)
    set(objects_d3d10_1core ${arch}-d3d10core:guest/d3d10core/kmt.c)
    set(libs_wintrust ${MIME_LIB_${arch}}/crypt32.lib)
    set(libs_msacm32 ${MIME_LIB_${arch}}/winmm.lib)
    set(includes_dxgi third_party/khronos)
    set(includes_d3d11 third_party/khronos)
    set(includes_d3d12 third_party/khronos third_party/SPIRV-Cross)
    foreach(entry IN LISTS MIME_EXTRA_DLLS)
        string(REPLACE ":" ";" entry ${entry})
        list(GET entry 0 name)
        list(GET entry 1 base)
        set(file ${name}.dll)
        if(name STREQUAL "winspool")
            set(file winspool.drv)
        endif()
        mime_glob(sources guest/${name}/*.c guest/${name}/*.cpp guest/${name}/${arch}/*.S)
        mime_dll(${name} ${arch} BASE ${base} FILE ${file} SOURCES ${sources} OBJECTS ${arch}-chkstk ${objects_${name}}
            INCLUDES ${includes_${name}} IMPORTS ntdll kernel32 user32 gdi32 win32u LIBS ${libs_${name}})
    endforeach()
endforeach()

# the acm codecs windows registers in Drivers32, drivers in files of their own.
set(MIME_ACM_CODECS msg711:0x7FF360000000 imaadp32:0x7FF350000000 msadp32:0x7FF340000000 msgsm32:0x7FF330000000)
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

mime_glob(MIME_MMDEVAPI_SOURCES guest/mmdevapi/*.c)

# opengl on the host gpu: a native dll without imports, so the translator can map it into x64 processes
# too, where an x64 opengl32.dll of stubs forwards each call to it, see mime/mx64.h and tools/gengl.c.
set(opengl32_options -Wno-ignored-attributes -DNTSYSCALLAPI= -DW32KAPI=)
set(opengl32_includes guest/opengl32 third_party/khronos)
if(MIME_GUEST_ARCH STREQUAL "arm64")
    mime_dll(opengl32 arm64 BASE 0x7FF550000000 ENTRY /noentry
        SOURCES ${MIME_OPENGL32_SOURCES} ${MIME_GL_GEN}/procs.c ${MIME_GL_GEN}/x64_thunks.c
        OBJECTS arm64-ntsyscalls arm64-win32ksyscalls ${MIME_RUNTIME_arm64} OPTIONS ${opengl32_options}
        INCLUDES ${opengl32_includes} DEF ${MIME_GL_GEN}/exports.def NOIMPLIB)
    mime_dll(opengl32 x64 BASE 0x7FF540000000
        SOURCES guest/opengl32/x64/main.c guest/opengl32/x64/native.S ${MIME_GL_GEN}/x64_stubs.S
        OBJECTS x64-chkstk OPTIONS ${opengl32_options} INCLUDES ${opengl32_includes} IMPORTS ntdll kernel32
        DEF ${MIME_GL_GEN}/x64.def NOIMPLIB)
else()
    # an x64 machine runs the whole implementation natively, like the arm64 one.
    mime_dll(opengl32 x64 BASE 0x7FF550000000 ENTRY /noentry
        SOURCES ${MIME_OPENGL32_SOURCES} ${MIME_GL_GEN}/procs.c
        OBJECTS x64-ntsyscalls x64-win32ksyscalls ${MIME_RUNTIME_x64} OPTIONS ${opengl32_options}
        INCLUDES ${opengl32_includes} DEF ${MIME_GL_GEN}/native.def NOIMPLIB)
endif()

if(MIME_GUEST_ARCH STREQUAL "x64")
    # the vulkan api over the wire to the host's driver.
    mime_glob(MIME_VULKAN_SOURCES guest/vulkan-1/*.c)
    set(MIME_VULKAN_GEN_SOURCES mvk_structs.c mvk_guest_commands.c mvk_guest_out.c mvk_guest_procs.c)
    list(TRANSFORM MIME_VULKAN_GEN_SOURCES PREPEND ${MIME_VK_GEN}/)
    set(MIME_VULKAN_OPTIONS -DMVK_GUEST=1 -Wno-unused-variable -Wno-unused-function -Wno-unused-but-set-variable)
    set(MIME_VULKAN_INCLUDES third_party/khronos ${MIME_VK_GEN})
    mime_dll(vulkan-1 x64 BASE 0x7FF420000000 SOURCES ${MIME_VULKAN_SOURCES} ${MIME_VULKAN_GEN_SOURCES}
        OBJECTS x64-chkstk OPTIONS ${MIME_VULKAN_OPTIONS} INCLUDES ${MIME_VULKAN_INCLUDES}
        IMPORTS ntdll kernel32 win32u user32 DEF ${MIME_VK_GEN}/mvk_exports.def DEPENDS mime-gen-vulkan)

    # wow64, 32-bit programs on x64: wow64.dll and wow64win.dll share the conversions of the translator, and
    # wow64cpu.dll sits below 4 GiB, where 32-bit code can jump back to it.
    mime_objects(x64-wow32 x64 SOURCES guest/mimex86/wow32.c guest/mimex86/wow32_win32k.c guest/mimex86/wow32_callback.c
        OPTIONS -fno-math-errno INCLUDES guest/mimex86 DEPENDS mime-gen-win32k-calls)
    mime_sources(sources wow64 x64)
    mime_dll(wow64 x64 BASE 0x7FF420000000 SOURCES ${sources} OBJECTS x64-chkstk x64-wow32:guest/mimex86/wow32.c
        INCLUDES guest/mimex86 IMPORTS ntdll)
    mime_sources(sources wow64win x64)
    mime_dll(wow64win x64 BASE 0x7FF410000000 SOURCES ${sources}
        OBJECTS x64-chkstk x64-wow32:guest/mimex86/wow32_win32k.c x64-wow32:guest/mimex86/wow32_callback.c
        INCLUDES guest/mimex86 IMPORTS ntdll wow64 DEPENDS mime-gen-win32k-calls)
    mime_sources(sources wow64cpu x64)
    mime_dll(wow64cpu x64 BASE 0x78000000 SOURCES ${sources} OBJECTS x64-chkstk IMPORTS ntdll wow64)
endif()
