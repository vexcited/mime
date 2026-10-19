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
    foreach(lib privdata dxbc dxil png msstyles compound audio dinput xinput richedit crypto ttf)
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
    # a module's own resources, as the command tables of mci drivers, sit beside its sources.
    set(script)
    set(resource)
    if(EXISTS ${CMAKE_SOURCE_DIR}/guest/${name}/${name}.rc)
        set(script RESOURCE_SCRIPT ${CMAKE_SOURCE_DIR}/guest/${name}/${name}.rc)
        set(resource RESOURCE ${arg_FILE})
    endif()
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
            if(EXISTS ${CMAKE_SOURCE_DIR}/guest/${name}/x86/${name}.def)
                list(APPEND own_def guest/${name}/x86/${name}.def)
            endif()
            add_custom_command(OUTPUT ${def} ${imports_def}
                COMMAND Python3::Interpreter tools/genx86exports.py ${library} ${MIME_GEN}/x86/${name} ${reference}
                    ${object_files}
                DEPENDS tools/genx86exports.py ${reference} ${object_files} ${object_targets} ${libs} ${own_def}
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} COMMAND_EXPAND_LISTS VERBATIM)
        endif()
        if(arg_NOIMPLIB)
            mime_link(${output} x86 OBJECTS ${objects} LIBS ${libs} FLAGS ${flags} /def:${def} /noimplib
                DEPENDS ${def} ${depends} ${resource} ${script} GROUPS guest)
        else()
            mime_link(${output} x86 OBJECTS ${objects} LIBS ${libs} FLAGS ${flags} /def:${def}
                DEPENDS ${def} ${imports_def} ${depends} IMPLIB ${implib} IMPORTS ${imports_def} ${resource} ${script}
                GROUPS guest)
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
                RESOURCE ${arg_FILE} ${script} GROUPS guest)
        else()
            mime_link(${output} ${arch} OBJECTS ${objects} LIBS ${libs} FLAGS ${flags} DEPENDS ${depends}
                IMPLIB ${implib} RESOURCE ${arg_FILE} ${script} GROUPS guest)
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

set(nt_export_code "^(Rtl|Ldr|Dbg|vDbg|Etw|Tp|Nls|Csr|Wer|MimeSxs|ApiSet)[A-Z]|^KiUser(Exception|Apc|Callback)Dispatcher$|^LdrInitializeThunk$|^VerSetConditionMask$|^(_?str|_?wcs|mem|_vsn|_sn|sprintf|swprintf|to|tow|__chkstk|_i64tow|_itow|_ultow|_wtoi|atoi|_wcs|_str)")
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
    mime_objects(${arch}-ntdll ${arch} SOURCES ${ntdll_sources} guest/lib/crypto/hash.c INCLUDES guest/ntdll guest/lib)
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
set(MIME_EXTRA_DLLS msvcrt:0x7FF6F0000000 advapi32:0x7FF6E0000000 sechost:0x7FF080000000 ws2_32:0x7FF6D0000000 iphlpapi:0x7FF6C0000000
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
    d3d9:0x7FF3D0000000 cfgmgr32:0x7FF2F0000000 setupapi:0x7FF3C0000000 d3d10core:0x7FF3B0000000 d3d10_1core:0x7FF3A0000000
    d3d10:0x7FF390000000 d3d10_1:0x7FF380000000 msacm32:0x7FF370000000 dsound:0x7FF320000000
    ddraw:0x7FF310000000 dinput:0x7FF300000000 hid:0x7FF2E0000000 xinput1_4:0x7FF2D0000000
    xinput1_3:0x7FF2C0000000 xinput1_2:0x7FF2B0000000 xinput1_1:0x7FF2A0000000 xinput9_1_0:0x7FF290000000
    winhttp:0x7FF240000000 windows_storage:0x7FF130000000 explorerframe:0x7FF120000000
    propsys:0x7FF230000000 shcore:0x7FF220000000 powrprof:0x7FF210000000 bcryptprimitives:0x7FF200000000
    pdh:0x7FF1F0000000 wevtapi:0x7FF1E0000000 tbs:0x7FF1D0000000 ncrypt:0x7FF1C0000000 credui:0x7FF1B0000000
    cryptui:0x7FF1A0000000 ndfapi:0x7FF190000000 winusb:0x7FF180000000 bthprops:0x7FF170000000
    dhcpcsvc:0x7FF160000000 esent:0x7FF150000000 uiautomationcore:0x7FF140000000
    msctf:0x7FF0D0000000 riched20:0x7FF0C0000000 msftedit:0x7FF0B0000000 riched32:0x7FF0A0000000
    dcomp:0x7FF030000000 prntvpt:0x7FF020000000 psapi:0x7FF0E0000000 lz32:0x7FF0F0000000
    mfplat:0x7FF060000000 mf:0x7FF050000000 mfreadwrite:0x7FF040000000 mfcaptureengine:0x7FF010000000 coremessaging:0x7FF090000000)

foreach(arch IN LISTS MIME_USER_ARCHS)
    # what some take from guest/lib or from another dll's sources.
    set(objects_msi ${arch}-lib-compound)
    set(objects_winmm ${arch}-lib-audio)
    set(objects_dsound ${arch}-lib-audio)
    # dinput8 and dinput are revisions of one engine.
    set(objects_dinput8 ${arch}-lib-dinput)
    set(objects_dinput ${arch}-lib-dinput)
    # the xinput dlls are revisions of one engine.
    foreach(xinput xinput1_4 xinput1_3 xinput1_2 xinput1_1)
        set(objects_${xinput} ${arch}-lib-xinput)
        set(libs_${xinput} ${MIME_LIB_${arch}}/setupapi.lib)
    endforeach()
    set(objects_gdiplus ${arch}-lib-png)
    # dwrite reads and draws fonts with the truetype library of the font engine.
    set(objects_dwrite ${arch}-lib-ttf)
    set(includes_dwrite guest/lib)
    # the cryptographic primitives: whole in bcrypt, crypt32 and secur32's tls, the hashes and chacha20 in advapi32.
    set(objects_bcrypt ${arch}-lib-crypto)
    set(objects_crypt32 ${arch}-lib-crypto)
    set(objects_secur32 ${arch}-lib-crypto)
    set(objects_advapi32 ${arch}-lib-crypto:guest/lib/crypto/hash.c ${arch}-lib-crypto:guest/lib/crypto/stream.c
        ${arch}-lib-crypto:guest/lib/crypto/bignum.c ${arch}-lib-crypto:guest/lib/crypto/rsa.c)
    foreach(crypto bcrypt crypt32 secur32 advapi32)
        set(includes_${crypto} guest/lib)
    endforeach()
    # riched20 and msftedit are one rich edit engine; riched32 wraps riched20 as windows' does.
    set(objects_riched20 ${arch}-lib-richedit)
    set(objects_msftedit ${arch}-lib-richedit)
    set(includes_riched20 guest/lib)
    set(includes_msftedit guest/lib)
    set(libs_riched32 ${MIME_LIB_${arch}}/riched20.lib)
    # winhttp inflates gzip and deflate bodies with the png decoder's inflate.
    set(objects_winhttp ${arch}-lib-png:guest/lib/png/inflate.c)
    # winspool writes the pages of a PDF flate coded.
    set(objects_winspool ${arch}-lib-png:guest/lib/png/deflate.c ${arch}-lib-png:guest/lib/png/inflate.c)
    set(includes_winspool guest/lib)
    set(includes_winhttp guest/lib)
    set(libs_winhttp ${MIME_LIB_${arch}}/ws2_32.lib)
    # wininet too, and reaches servers through winsock.
    set(objects_wininet ${arch}-lib-png:guest/lib/png/inflate.c)
    set(includes_wininet guest/lib)
    set(libs_wininet ${MIME_LIB_${arch}}/ws2_32.lib ${MIME_LIB_${arch}}/kernelbase.lib)
    # urlmon downloads through wininet, as windows' does, and hands out com streams and strings.
    set(libs_urlmon ${MIME_LIB_${arch}}/wininet.lib ${MIME_LIB_${arch}}/ole32.lib ${MIME_LIB_${arch}}/oleaut32.lib)
    set(objects_dxgi ${arch}-lib-privdata)
    set(objects_d3d11 ${arch}-lib-privdata ${arch}-lib-dxbc)
    # d3d12 compiles dxbc as d3d11 does, dxil too, and shares its table of formats.
    set(objects_d3d12 ${arch}-lib-privdata ${arch}-lib-dxbc ${arch}-lib-dxil ${arch}-d3d11:guest/d3d11/format.c ${arch}-d3d11:guest/d3d11/vkprofile.c ${arch}-d3d11:guest/d3d11/gfxshare.c)
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
    set(libs_setupapi ${MIME_LIB_${arch}}/cfgmgr32.lib)
    # shell32 appends to paths with the path functions of kernelbase.
    set(libs_shell32 ${MIME_LIB_${arch}}/kernelbase.lib)
    set(libs_propsys ${MIME_LIB_${arch}}/ole32.lib ${MIME_LIB_${arch}}/oleaut32.lib)
    set(libs_shcore ${MIME_LIB_${arch}}/ole32.lib)
    # the common dialogs are built from common controls, shell items and shlwapi's path helpers, and ask the registry
    # which file extensions are known.
    set(libs_comdlg32 ${MIME_LIB_${arch}}/comctl32.lib ${MIME_LIB_${arch}}/shell32.lib
        ${MIME_LIB_${arch}}/shlwapi.lib ${MIME_LIB_${arch}}/ole32.lib ${MIME_LIB_${arch}}/advapi32.lib)
    # windows.storage makes shell item lists with shell32, keeps properties in propsys and loads strings with shlwapi.
    set(libs_windows_storage ${MIME_LIB_${arch}}/ole32.lib ${MIME_LIB_${arch}}/shell32.lib
        ${MIME_LIB_${arch}}/shlwapi.lib ${MIME_LIB_${arch}}/propsys.lib)
    # media foundation: mfplat's objects keep their values in ole32's property variants, mfreadwrite builds on mfplat.
    set(libs_mfplat ${MIME_LIB_${arch}}/ole32.lib)
    set(libs_mfreadwrite ${MIME_LIB_${arch}}/mfplat.lib ${MIME_LIB_${arch}}/ole32.lib)
    # the capture devices of mf make attributes, events and samples with mfplat, find cameras with cfgmgr32 and hand
    # out task memory.
    set(libs_mf ${MIME_LIB_${arch}}/mfplat.lib ${MIME_LIB_${arch}}/cfgmgr32.lib ${MIME_LIB_${arch}}/ole32.lib)
    # the capture engine drives a capture source made by mf, with mfplat's events and samples.
    set(libs_mfcaptureengine ${MIME_LIB_${arch}}/mfplat.lib ${MIME_LIB_${arch}}/mf.lib ${MIME_LIB_${arch}}/ole32.lib)
    # comctl32 draws through uxtheme, as windows' imports it.
    set(libs_comctl32 ${MIME_LIB_${arch}}/uxtheme.lib)
    # msctf hands out task memory and bstrs and keeps values in variants.
    set(libs_msctf ${MIME_LIB_${arch}}/ole32.lib ${MIME_LIB_${arch}}/oleaut32.lib)
    # oleacc marshals its objects through ole32 and speaks in oleaut32's strings and variants.
    set(libs_oleacc ${MIME_LIB_${arch}}/ole32.lib ${MIME_LIB_${arch}}/oleaut32.lib)
    set(includes_dxgi third_party/khronos)
    # the video processor's compute shader is assembled at build time.
    set(includes_d3d11 third_party/khronos ${MIME_SHADERS_GEN})
    set(depends_d3d11 mime-gen-shaders)
    set(includes_d3d12 third_party/khronos third_party/SPIRV-Cross)
    # dcomp draws with shaders assembled at build time.
    set(includes_dcomp ${MIME_SHADERS_GEN})
    set(depends_dcomp mime-gen-shaders)
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
        endif()
        mime_glob(sources guest/${name}/*.c guest/${name}/*.cpp guest/${name}/${arch}/*.S)
        mime_dll(${name} ${arch} BASE ${base} FILE ${file} SOURCES ${sources} OBJECTS ${arch}-chkstk ${objects_${name}}
            INCLUDES ${includes_${name}} IMPORTS ntdll kernel32 user32 gdi32 win32u LIBS ${libs_${name}}
            DEPENDS ${depends_${name}})
    endforeach()
endforeach()

# services.exe, the service control manager: a program of the system, started at the boot of the machine from the
# native System32. it reads the services database of the registry and serves advapi32's service functions.
mime_glob(services_sources guest/services/*.c)
set(services_arch ${MIME_GUEST_ARCH})
mime_objects(${services_arch}-services ${services_arch} SOURCES ${services_sources} INCLUDES guest/services guest/lib)
mime_link(${MIME_OUT_${services_arch}}/services.exe ${services_arch}
    OBJECTS ${services_arch}-services ${services_arch}-chkstk ${services_arch}-lib-crypto:guest/lib/crypto/hash.c
    LIBS ${MIME_LIB_${services_arch}}/ntdll.lib ${MIME_LIB_${services_arch}}/kernel32.lib
        ${MIME_LIB_${services_arch}}/advapi32.lib
    FLAGS /subsystem:console,10.0 /entry:entry /base:0x7FF6A8000000 /highentropyva /dynamicbase /stack:0x100000,0x4000
    RESOURCE services.exe GROUPS guest)
mime_rom_file(${MIME_OUT_${services_arch}}/services.exe ${MIME_ROM_${services_arch}}/services.exe)

# svchost.exe, the host of the services that live in DLLs: the processor of the machine in System32, and the 32-bit one
# in SysWOW64 for the 32-bit dlls.
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
            FLAGS ${svchost_flags} RESOURCE svchost.exe GROUPS guest)
        mime_rom_file(${MIME_OUT_${arch}}/svchost.exe ${MIME_ROM_${arch}}/svchost.exe)
    endif()
endforeach()

# the acm codecs windows registers in Drivers32, drivers in files of their own.
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

# the mci drivers of [MCI32], device drivers in dlls of their own.
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
    mime_objects(x64-wow32 x64 SOURCES guest/mimex86/wow32.c guest/mimex86/wow32_query.c guest/mimex86/wow32_win32k.c
        guest/mimex86/wow32_callback.c
        OPTIONS -fno-math-errno INCLUDES guest/mimex86 DEPENDS mime-gen-win32k-calls)
    mime_sources(sources wow64 x64)
    mime_dll(wow64 x64 BASE 0x7FF420000000 SOURCES ${sources}
        OBJECTS x64-chkstk x64-wow32:guest/mimex86/wow32.c x64-wow32:guest/mimex86/wow32_query.c
        INCLUDES guest/mimex86 IMPORTS ntdll)
    mime_sources(sources wow64win x64)
    mime_dll(wow64win x64 BASE 0x7FF410000000 SOURCES ${sources}
        OBJECTS x64-chkstk x64-wow32:guest/mimex86/wow32_win32k.c x64-wow32:guest/mimex86/wow32_callback.c
        INCLUDES guest/mimex86 IMPORTS ntdll wow64 DEPENDS mime-gen-win32k-calls)
    mime_sources(sources wow64cpu x64)
    mime_dll(wow64cpu x64 BASE 0x78000000 SOURCES ${sources} OBJECTS x64-chkstk IMPORTS ntdll wow64)
endif()
