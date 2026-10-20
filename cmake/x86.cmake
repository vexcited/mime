# x86 user mode, the same sources with 32-bit layouts and stdcall exports, into SysWOW64.

set(gen86 ${MIME_GEN}/x86)

# ntdll and its system call stubs.
mime_objects(x86-ntsyscalls x86 SOURCES ${gen86}/ntdll_syscalls.S)
mime_glob(sources guest/ntdll/*.c guest/ntdll/x86/*.c guest/ntdll/x86/*.S)
mime_objects(x86-ntdll x86 SOURCES ${sources} guest/lib/crypto/hash.c INCLUDES guest/ntdll guest/lib)
set(objects x86-ntdll x86-ntsyscalls ${MIME_RUNTIME_x86})
mime_object_list(files ${objects})
mime_object_targets(targets ${objects})
add_custom_command(OUTPUT ${gen86}/ntdll.def ${gen86}/ntdll.imports.def
    COMMAND Python3::Interpreter tools/genx86exports.py ntdll ${gen86}/ntdll ${MIME_NATIVE_x86_REF}/ntdll.dll ${files}
    DEPENDS tools/genx86exports.py ${MIME_NATIVE_x86_REF}/ntdll.dll ${files} ${targets}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} COMMAND_EXPAND_LISTS VERBATIM)
mime_dll(ntdll x86 BASE 0x77000000 ENTRY /noentry OBJECTS ${objects} DEF ${gen86}/ntdll.def
    IMPORTS_DEF ${gen86}/ntdll.imports.def DEPENDS ${gen86}/ntdll.imports.def)

mime_sources(sources kernelbase x86)
mime_dll(kernelbase x86 BASE 0x76000000 SOURCES ${sources} OBJECTS x86-chkstk OPTIONS -DMIME_BUILD_KERNELBASE
    IMPORTS ntdll)

# kernel32 forwards to kernelbase, but for what only x86's kernel32 exports itself, stdcall decorated.
set(kernel32_own timeGetTime@0 timeGetSystemTime@8
    timeGetDevCaps@8 timeBeginPeriod@4 timeEndPeriod@4 DnsHostnameToComputerNameW@12)
# the transacted file functions, with the bytes of arguments each takes.
foreach(name CopyFileTransacted:28 CreateDirectoryTransacted:16 CreateFileTransacted:40 CreateHardLinkTransacted:16
    CreateSymbolicLinkTransacted:16 DeleteFileTransacted:8 FindFirstFileTransacted:28 GetCompressedFileSizeTransacted:12
    GetFileAttributesTransacted:16 GetFullPathNameTransacted:20 GetLongPathNameTransacted:16 MoveFileTransacted:24
    RemoveDirectoryTransacted:8 SetFileAttributesTransacted:12)
    string(REPLACE ":" ";" name ${name})
    list(GET name 0 base)
    list(GET name 1 bytes)
    list(APPEND kernel32_own ${base}A@${bytes} ${base}W@${bytes})
endforeach()
list(APPEND kernel32_own FindFirstFileNameTransactedW@20 FindFirstStreamTransactedW@20 GetBinaryTypeA@8 GetBinaryTypeW@8
    GetBinaryType@8 SetSystemPowerState@8)
add_custom_command(OUTPUT ${gen86}/kernel32.def ${gen86}/kernel32.imports.def
    COMMAND Python3::Interpreter tools/gendef.py forward32 ${gen86}/kernel32.def ${gen86}/kernel32.imports.def
        ${MIME_SYSX86}/kernelbase.dll ${gen86}/kernelbase.imports.def ${kernel32_own}
    DEPENDS tools/gendef.py ${MIME_SYSX86}/kernelbase.dll ${gen86}/kernelbase.imports.def
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} VERBATIM)
mime_dll(kernel32 x86 BASE 0x75000000 SOURCES guest/kernel32/main.c guest/kernel32/transacted.c guest/kernel32/binary.c
    OPTIONS -DMIME_BUILD_KERNEL32 IMPORTS kernelbase ntdll DEF ${gen86}/kernel32.def
    IMPORTS_DEF ${gen86}/kernel32.imports.def DEPENDS ${gen86}/kernel32.imports.def)

# on x64 the stubs reach the 32-bit ntdll's Wow64Transition through a gate of their own.
set(sources ${gen86}/win32u_syscalls.S)
set(imports)
if(MIME_GUEST_ARCH STREQUAL "x64")
    list(APPEND sources guest/win32u/x86/wow64.S)
    set(imports ntdll)
endif()
mime_dll(win32u x86 BASE 0x74000000 ENTRY /noentry SOURCES ${sources} IMPORTS ${imports})

mime_sources(sources gdi32 x86)
mime_dll(gdi32 x86 BASE 0x73000000 SOURCES ${sources} OBJECTS x86-chkstk OPTIONS -DMIME_BUILD_GDI32
    IMPORTS ntdll kernel32 win32u)
mime_sources(sources user32 x86)
mime_dll(user32 x86 BASE 0x72000000 SOURCES ${sources} OBJECTS x86-chkstk x86-lib-png x86-lib-msstyles
    OPTIONS -DMIME_BUILD_USER32 INCLUDES guest/lib IMPORTS ntdll kernel32 win32u gdi32)

set(all_five ntdll kernel32 user32 gdi32 win32u)
set(MIME_X86_DLLS
    shlwapi:0x71000000:all_five shell32:0x70000000:all_five winmm:0x6f000000:all_five version:0x6e000000:all_five
    advapi32:0x6d000000:all_five sechost:0x0F000000:base ws2_32:0x6c000000:all_five uxtheme:0x6b000000:all_five
    netapi32:0x6a000000:all_five wtsapi32:0x69000000:all_five secur32:0x68000000:all_five
    crypt32:0x67000000:all_five userenv:0x66000000:all_five ole32:0x65000000:ole32_imports oleaut32:0x64000000:oleaut32_imports
    rpcrt4:0x63000000:rpcrt4_imports imm32:0x62000000:all_five comctl32:0x61000000:comctl comdlg32:0x60000000:dialog_imports
    msi:0x5f000000:msi_imports mpr:0x5e000000:base wintrust:0x5d000000:crypt odbc32:0x5c000000:user
    gdiplus:0x5b000000:gdiplus_imports wininet:0x5a000000:wininet_imports urlmon:0x59000000:urlmon_imports msimg32:0x58000000:gdi
    oleacc:0x57000000:oleacc_imports oledlg:0x56000000:user combase:0x54000000:all_five usp10:0x51000000:gdi
    cfgmgr32:0x39000000:base setupapi:0x46000000:setup msacm32:0x42000000:acm dsound:0x3D000000:all_five ddraw:0x3C000000:direct3d
    dinput8:0x3B000000:all_five dinput:0x3A000000:all_five hid:0x38000000:base xinput1_4:0x37000000:xinput
    xinput1_3:0x36000000:xinput xinput1_2:0x35000000:xinput xinput1_1:0x34000000:xinput xinput9_1_0:0x33000000:base
    winhttp:0x2C000000:http windows_storage:0x15000000:storage_imports explorerframe:0x14000000:all_five
    dwmapi:0x13000000:all_five prntvpt:0x11000000:all_five psapi:0x0D000000:base lz32:0x0C000000:base wsock32:0x0B000000:http mswsock:0x0A000000:base
    propsys:0x2B000000:propsys_imports shcore:0x2A000000:shcore_imports powrprof:0x29000000:all_five
    bcryptprimitives:0x28000000:all_five pdh:0x27000000:all_five wevtapi:0x26000000:all_five tbs:0x25000000:all_five
    ncrypt:0x24000000:all_five credui:0x23000000:all_five cryptui:0x22000000:all_five ndfapi:0x21000000:all_five
    winusb:0x20000000:all_five dhcpcsvc:0x1E000000:all_five esent:0x1D000000:all_five
    uiautomationcore:0x1C000000:all_five iphlpapi:0x1B000000:base bcrypt:0x1A000000:all_five
    mfplat:0x12400000:mfplat_imports mf:0x12200000:mf_imports mfreadwrite:0x12000000:mfreadwrite_imports
    mfcaptureengine:0x12100000:mfce_imports
    schannel:0x19000000:base msctf:0x0E000000:ctf_imports activeds:0x47000000:ads_imports advpack:0x10000000:advpack_imports windowscodecs:0x4B000000:wic_imports)
set(base ntdll kernel32)
# rpcrt4 catches exceptions with frame handlers of the c runtime.
set(rpcrt4_imports ntdll kernel32 user32 gdi32 win32u msvcrt)
# ole32 reaches other processes with rpc, and catches the faults of stubs with the frame handlers of the c runtime.
set(ole32_imports ntdll kernel32 user32 gdi32 win32u rpcrt4 msvcrt)
set(depends_ole32 mime-gen-ole32)
set(depends_oleaut32 mime-gen-oleaut32)
# oleaut32 makes objects with ole32 and asks it for guid strings.
set(oleaut32_imports ntdll kernel32 user32 gdi32 win32u ole32 rpcrt4)
set(propsys_imports ntdll kernel32 user32 gdi32 win32u ole32 oleaut32)
# the imaging component makes bitmaps of handles with gdi, and reads icons with user32.
set(wic_imports ntdll kernel32 user32 gdi32 win32u ole32)
set(shcore_imports ntdll kernel32 user32 gdi32 win32u ole32)
set(dialog_imports ntdll kernel32 user32 gdi32 win32u comctl32 shell32 shlwapi ole32 advapi32)
set(storage_imports ntdll kernel32 user32 gdi32 win32u ole32 shell32 shlwapi propsys)
set(comctl ntdll kernel32 user32 gdi32 win32u uxtheme)
set(mfplat_imports ntdll kernel32 ole32)
set(mfreadwrite_imports ntdll kernel32 mfplat ole32)
set(mf_imports ntdll kernel32 mfplat cfgmgr32 ole32)
set(mfce_imports ntdll kernel32 mfplat mf ole32)
set(user ntdll kernel32 user32)
set(ads_imports ntdll kernel32)
set(advpack_imports ntdll kernel32 advapi32 version)
set(objects_advpack x86-lib-cabinet)
# the installer makes shortcuts and users, reads versions and loads the custom actions of its packages.
set(msi_imports ntdll kernel32 user32 gdi32 win32u advapi32 shell32 ole32 version)
set(oleacc_imports ntdll kernel32 user32 ole32 oleaut32)
set(ctf_imports ntdll kernel32 user32 gdi32 win32u ole32 oleaut32)
set(gdi ntdll kernel32 gdi32)
set(user_gdi ntdll kernel32 user32 gdi32)
set(gdiplus_imports ntdll kernel32 user32 gdi32 ole32)
set(crypt ntdll kernel32 crypt32)
set(acm ntdll kernel32 user32 gdi32 winmm)
# direct3d 1 to 7 in ddraw, whose front end takes its math from msvcrt as windows' does.
set(direct3d ntdll kernel32 user32 gdi32 win32u msvcrt)
set(setup ntdll kernel32 cfgmgr32 advapi32 version)
set(objects_setupapi x86-lib-cabinet)
set(xinput ntdll kernel32 user32 setupapi)
set(http ntdll kernel32 user32 ws2_32)
# wininet combines urls with the engine of kernelbase.
set(wininet_imports ntdll kernel32 user32 ws2_32 kernelbase)
set(urlmon_imports ntdll kernel32 user32 wininet ole32 oleaut32)
# oleaut32 divides 64 bit numbers by one another in its conversions.
set(objects_oleaut32 x86-div)
set(objects_msi x86-lib-compound x86-lib-cabinet)
set(objects_winmm x86-lib-audio)
set(objects_dsound x86-lib-audio)
set(objects_dinput8 x86-lib-dinput)
set(objects_dinput x86-lib-dinput)
foreach(xinput xinput1_4 xinput1_3 xinput1_2 xinput1_1)
    set(objects_${xinput} x86-lib-xinput)
endforeach()
set(objects_gdiplus x86-lib-vector x86-div)
set(includes_gdiplus guest/lib)
set(objects_windowscodecs x86-lib-png:guest/lib/png/inflate.c x86-lib-png:guest/lib/png/deflate.c x86-lib-pngio x86-lib-jpeg x86-lib-tiff x86-lib-gif x86-lib-bcn x86-div)
set(includes_windowscodecs guest/lib)
set(objects_bcrypt x86-lib-crypto)
set(objects_crypt32 x86-lib-crypto)
set(objects_secur32 x86-lib-crypto)
set(objects_advapi32 x86-lib-crypto:guest/lib/crypto/hash.c x86-lib-crypto:guest/lib/crypto/stream.c
        x86-lib-crypto:guest/lib/crypto/bignum.c x86-lib-crypto:guest/lib/crypto/rsa.c)
foreach(crypto bcrypt crypt32 secur32 advapi32)
    set(includes_${crypto} guest/lib)
endforeach()
set(objects_winhttp x86-lib-png:guest/lib/png/inflate.c)
set(includes_winhttp guest/lib)
set(objects_wininet x86-lib-png:guest/lib/png/inflate.c)
set(includes_wininet guest/lib)

if(MIME_GUEST_ARCH STREQUAL "x64")
    # vulkan-1, dxgi and direct3d for 32-bit programs on the linux host: the same sources with 32-bit
    # layouts, which speak the same wire.
    mime_dll(vulkan-1 x86 BASE 0x53000000 SOURCES ${MIME_VULKAN_SOURCES} ${MIME_VULKAN_GEN_SOURCES}
        OBJECTS x86-chkstk OPTIONS ${MIME_VULKAN_OPTIONS} INCLUDES ${MIME_VULKAN_INCLUDES}
        IMPORTS ntdll kernel32 win32u user32 DEPENDS mime-gen-vulkan)
    list(APPEND MIME_X86_DLLS dxgi:0x52000000:all_five d3d11:0x50000000:all_five d3dcompiler_47:0x4F000000:all_five
        d3d10core:0x4E000000:base d3d10_1core:0x4D000000:base d3d10:0x4C000000:all_five d3d10_1:0x4B000000:all_five
        d3d12:0x4A000000:all_five d3d9:0x49000000:all_five dcomp:0x13000000:all_five)
    set(objects_dxgi x86-lib-privdata)
    set(objects_d3d11 x86-lib-privdata x86-lib-dxbc)
    set(objects_d3dcompiler_47 x86-lib-dxbc:guest/lib/dxbc/container.c x86-lib-dxbc:guest/lib/dxbc/reader.c)
    set(objects_d3d10 x86-lib-dxbc:guest/lib/dxbc/container.c)
    set(objects_d3d10_1 x86-d3d10:guest/d3d10/create.c)
    set(objects_d3d10_1core x86-d3d10core:guest/d3d10core/kmt.c)
    set(objects_d3d12 x86-lib-privdata x86-lib-dxbc x86-lib-dxil x86-d3d11:guest/d3d11/format.c x86-d3d11:guest/d3d11/vkprofile.c x86-d3d11:guest/d3d11/gfxshare.c)
    set(objects_d3d9 x86-lib-privdata x86-lib-dxbc:guest/lib/dxbc/container.c)
    set(includes_dxgi third_party/khronos)
    # the video processor's compute shader is assembled at build time.
    set(includes_d3d11 third_party/khronos ${MIME_SHADERS_GEN})
    set(depends_d3d11 mime-gen-shaders)
    set(includes_d3d12 third_party/khronos third_party/SPIRV-Cross)
    # dcomp draws with shaders assembled at build time.
    set(includes_dcomp ${MIME_SHADERS_GEN})
    set(depends_dcomp mime-gen-shaders)
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
    endif()
    mime_dll(${name} x86 BASE ${address} FILE ${file} SOURCES ${sources} OBJECTS x86-chkstk ${objects_${name}}
        INCLUDES ${includes_${name}} IMPORTS ${${imports}} DEPENDS ${depends_${name}})
endforeach()

if(MIME_GUEST_ARCH STREQUAL "x64")
    # direct3d 8 over the 32-bit d3d9. windows has no 64-bit d3d8 to take the exports from, so its .def is ours.
    mime_sources(sources d3d8 x86)
    mime_dll(d3d8 x86 BASE 0x32000000 SOURCES ${sources} OBJECTS x86-chkstk x86-lib-privdata
        DEF ${CMAKE_SOURCE_DIR}/guest/d3d8/d3d8.def IMPORTS_DEF ${CMAKE_SOURCE_DIR}/guest/d3d8/d3d8.imports.def
        IMPORTS ntdll kernel32 user32 gdi32 d3d9)
endif()

# the rich edit control: riched20 and msftedit share one engine and list their exports by hand, with the
# ordinals and data exports windows' have; riched32 registers its class over riched20's procedure.
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

# winspool is a driver file.
mime_glob(sources guest/winspool/*.c)
mime_dll(winspool x86 BASE 0x55000000 FILE winspool.drv SOURCES ${sources}
    OBJECTS x86-chkstk x86-lib-png:guest/lib/png/deflate.c x86-lib-png:guest/lib/png/inflate.c INCLUDES guest/lib
    IMPORTS ntdll kernel32 user32 gdi32)

# bthprops is a control panel file.
mime_sources(sources bthprops x86)
mime_dll(bthprops x86 BASE 0x1F000000 FILE bthprops.cpl SOURCES ${sources} OBJECTS x86-chkstk IMPORTS ${all_five})

# msvcrt keeps its own definitions: the x64 list where x86 defines the same, the helpers only x86 has, and what
# the native build exports through dllexport.
mime_sources(sources msvcrt x86)
mime_objects(x86-msvcrt x86 SOURCES ${sources} INCLUDES guest/msvcrt)
set(objects x86-msvcrt x86-chkstk x86-div)
mime_object_list(files ${objects})
mime_object_targets(targets ${objects})
add_custom_command(OUTPUT ${gen86}/msvcrt.def ${gen86}/msvcrt.imports.def
    COMMAND Python3::Interpreter tools/genx86def.py msvcrt ${gen86}/msvcrt
        guest/msvcrt/msvcrt.def,guest/msvcrt/x86/msvcrt.def,${MIME_NATIVE_x86_REF}/msvcrt.dll ${files}
    DEPENDS tools/genx86def.py guest/msvcrt/msvcrt.def guest/msvcrt/x86/msvcrt.def ${MIME_NATIVE_x86_REF}/msvcrt.dll
        ${files} ${targets}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} COMMAND_EXPAND_LISTS VERBATIM)
mime_dll(msvcrt x86 BASE 0x48000000 OBJECTS ${objects} IMPORTS ntdll kernel32 DEF ${gen86}/msvcrt.def
    IMPORTS_DEF ${gen86}/msvcrt.imports.def DEPENDS ${gen86}/msvcrt.imports.def)

# ucrtbase forwards to msvcrt as on x64.
mime_objects(x86-ucrtbase x86 SOURCES guest/ucrtbase/main.c INCLUDES guest/ucrtbase)
set(objects x86-ucrtbase x86-chkstk)
mime_object_list(files ${objects})
mime_object_targets(targets ${objects})
add_custom_command(OUTPUT ${gen86}/ucrtbase.def ${gen86}/ucrtbase.imports.def
    COMMAND Python3::Interpreter tools/genx86def.py ucrtbase ${gen86}/ucrtbase guest/ucrtbase/ucrtbase.def ${files}
    DEPENDS tools/genx86def.py guest/ucrtbase/ucrtbase.def ${files} ${targets}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} COMMAND_EXPAND_LISTS VERBATIM)
mime_dll(ucrtbase x86 BASE 0x47000000 ENTRY /noentry OBJECTS ${objects} DEF ${gen86}/ucrtbase.def
    IMPORTS_DEF ${gen86}/ucrtbase.imports.def DEPENDS ${gen86}/ucrtbase.imports.def ${MIME_LIB_x86}/msvcrt.lib)

# opengl for 32-bit programs: the same implementation over the same gpu channel, its system calls through the
# 32-bit ntdll and win32u.
mime_dll(opengl32 x86 BASE 0x44000000 ENTRY /noentry SOURCES ${MIME_OPENGL32_SOURCES} ${MIME_GL_GEN}/procs.c
    OBJECTS ${MIME_RUNTIME_x86} OPTIONS ${opengl32_options} INCLUDES ${opengl32_includes} IMPORTS ntdll win32u)

# the audio endpoints of 32-bit programs.
mime_dll(mmdevapi x86 BASE 0x45000000 SOURCES ${MIME_MMDEVAPI_SOURCES} OBJECTS x86-chkstk
    IMPORTS ntdll kernel32 user32 gdi32 win32u)
mime_dll(avrt x86 BASE 0x43000000 SOURCES guest/avrt/main.c OBJECTS x86-chkstk IMPORTS ntdll kernel32 user32 gdi32 win32u)

# the mci drivers of 32-bit programs.
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

# the acm codecs of 32-bit programs.
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
