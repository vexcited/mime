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
    foreach(lib privdata dxbc dxil png pngio jpeg tiff gif bcn vector msstyles compound cabinet audio dinput xinput richedit crypto ttf xmldom cbs lzms msdelta winrt icc)
        mime_glob(lib_sources guest/lib/${lib}/*.c)
        mime_objects(${arch}-lib-${lib} ${arch} SOURCES ${lib_sources} INCLUDES ${lib_includes})
    endforeach()
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
    # the icons, cursors, bitmaps and message tables the libraries carry, generated into gen/resources.
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

    # the exports that only forward to the runtime are thunks, which arm64 does not have yet.
    set(mscoree_thunks)
    if(NOT arch STREQUAL "arm64")
        set(mscoree_thunks guest/mscoree/${arch}/common.S ${MIME_GEN}/${arch}/mscoree_thunks.S)
    endif()
    mime_dll(mscoree ${arch} BASE 0x7FF7B0000000 SOURCES guest/mscoree/main.c guest/kernelbase/features.c ${mscoree_thunks} OBJECTS ${arch}-chkstk
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
    rpcss:0x7FEFD0000000 rpcepmap:0x7FEFC0000000 wkscli:0x7FEE70000000 wkssvc:0x7FEE00000000 httpapi:0x7FEDE0000000 wbemcore:0x7FEDD0000000 loadperf:0x7FEDC0000000 webservices:0x7FEDA0000000 xolehlp:0x7FED90000000 vssapi:0x7FED80000000 xmllite:0x7FED70000000 d2d1:0x7FED20000000 mstask:0x7FED10000000 msado15:0x7FED00000000 scrrun:0x7FECF0000000 wshom:0x7FECE0000000 security:0x7FED60000000 clusapi:0x7FED50000000 resutils:0x7FED40000000 nsisvc:0x7FEDF0000000 keyiso:0x7FEDB0000000 feclient:0x7FEFB0000000 zipfldr:0x7FEFA0000000 avifil32:0x7FEF90000000 glu32:0x7FEF80000000 odbccp32:0x7FEE10000000 mscms:0x7FEE20000000 mapi32:0x7FEE30000000 hlink:0x7FEE40000000 authz:0x7FEE50000000 wldap32:0x7FEE60000000 ntdsapi:0x7FEE80000000 mlang:0x7FED30000000 query:0x7FEE90000000 fontsub:0x7FEEA0000000 tdh:0x7FEEB0000000 windows.networking:0x7FEEC0000000 dbgeng:0x7FEED0000000 wmvcore:0x7FEEE0000000 avicap32:0x7FEEF0000000 rstrtmgr:0x7FEF00000000 wlanapi:0x7FEF10000000 snmpapi:0x7FEF20000000 ktmw32:0x7FEF30000000 msvfw32:0x7FEF40000000 normaliz:0x7FEF50000000 cryptsp:0x7FEF60000000 shfolder:0x7FEF70000000)
list(APPEND MIME_EXTRA_DLLS wldp:0x7FECD0000000 rasapi32:0x7FEC10000000 uianimation:0x7FECB0000000
    windows.graphics:0x7FECA0000000)

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
    set(objects_dinput8 ${arch}-lib-dinput)
    set(objects_dinput ${arch}-lib-dinput)
    foreach(xinput xinput1_4 xinput1_3 xinput1_2 xinput1_1)
        set(objects_${xinput} ${arch}-lib-xinput)
        set(libs_${xinput} ${MIME_LIB_${arch}}/setupapi.lib)
    endforeach()
    set(objects_d2d1 ${arch}-lib-vector ${arch}-lib-dxbc:guest/lib/dxbc/container.c ${arch}-lib-icc)
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
    set(objects_windowscodecs ${arch}-lib-png:guest/lib/png/inflate.c ${arch}-lib-png:guest/lib/png/deflate.c
        ${arch}-lib-pngio ${arch}-lib-jpeg ${arch}-lib-tiff ${arch}-lib-gif ${arch}-lib-bcn ${arch}-lib-icc)
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
    set(objects_d3dcompiler_47 ${arch}-lib-dxbc:guest/lib/dxbc/container.c ${arch}-lib-dxbc:guest/lib/dxbc/reader.c)
    set(objects_d3d9 ${arch}-lib-privdata ${arch}-lib-dxbc:guest/lib/dxbc/container.c)
    set(objects_d3d10 ${arch}-lib-dxbc:guest/lib/dxbc/container.c)
    set(objects_d3d10_1 ${arch}-d3d10:guest/d3d10/create.c)
    set(objects_d3d10_1core ${arch}-d3d10core:guest/d3d10core/kmt.c)
    set(libs_wintrust ${MIME_LIB_${arch}}/crypt32.lib)
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
    set(libs_vbscript ${MIME_LIB_${arch}}/ole32.lib ${MIME_LIB_${arch}}/oleaut32.lib ${MIME_LIB_${arch}}/advapi32.lib
        ${MIME_LIB_${arch}}/msvcrt.lib)
    set(libs_jscript ${MIME_LIB_${arch}}/ole32.lib ${MIME_LIB_${arch}}/oleaut32.lib ${MIME_LIB_${arch}}/advapi32.lib
        ${MIME_LIB_${arch}}/msvcrt.lib)
    set(libs_propsys ${MIME_LIB_${arch}}/ole32.lib ${MIME_LIB_${arch}}/oleaut32.lib ${MIME_LIB_${arch}}/advapi32.lib
        ${MIME_LIB_${arch}}/shlwapi.lib ${MIME_LIB_${arch}}/combase.lib)
    set(objects_propsys ${arch}-lib-crypto:guest/lib/crypto/hash.c)
    set(includes_propsys guest/lib)
    set(libs_shcore ${MIME_LIB_${arch}}/ole32.lib)
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
