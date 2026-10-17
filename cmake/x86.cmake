# x86 user mode, the same sources with 32-bit layouts and stdcall exports, into SysWOW64.

set(gen86 ${MIME_GEN}/x86)

# ntdll and its system call stubs.
mime_objects(x86-ntsyscalls x86 SOURCES ${gen86}/ntdll_syscalls.S)
mime_glob(sources guest/ntdll/*.c guest/ntdll/x86/*.c guest/ntdll/x86/*.S)
mime_objects(x86-ntdll x86 SOURCES ${sources} INCLUDES guest/ntdll)
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
set(kernel32_own InterlockedIncrement@4 InterlockedDecrement@4 InterlockedExchange@8 InterlockedExchangeAdd@8
    InterlockedCompareExchange@12 InterlockedCompareExchange64@20)
add_custom_command(OUTPUT ${gen86}/kernel32.def ${gen86}/kernel32.imports.def
    COMMAND Python3::Interpreter tools/gendef.py forward32 ${gen86}/kernel32.def ${gen86}/kernel32.imports.def
        ${MIME_SYSX86}/kernelbase.dll ${gen86}/kernelbase.imports.def ${kernel32_own}
    DEPENDS tools/gendef.py ${MIME_SYSX86}/kernelbase.dll ${gen86}/kernelbase.imports.def
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} VERBATIM)
mime_dll(kernel32 x86 BASE 0x75000000 SOURCES guest/kernel32/main.c guest/kernel32/x86/interlocked.S
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
    advapi32:0x6d000000:all_five ws2_32:0x6c000000:all_five uxtheme:0x6b000000:all_five
    netapi32:0x6a000000:all_five wtsapi32:0x69000000:all_five secur32:0x68000000:all_five
    crypt32:0x67000000:all_five userenv:0x66000000:all_five ole32:0x65000000:all_five oleaut32:0x64000000:all_five
    rpcrt4:0x63000000:all_five imm32:0x62000000:all_five comctl32:0x61000000:all_five comdlg32:0x60000000:all_five
    msi:0x5f000000:user mpr:0x5e000000:base wintrust:0x5d000000:crypt odbc32:0x5c000000:user
    gdiplus:0x5b000000:user_gdi wininet:0x5a000000:user urlmon:0x59000000:user msimg32:0x58000000:gdi
    oleacc:0x57000000:user oledlg:0x56000000:user combase:0x54000000:user usp10:0x51000000:gdi
    setupapi:0x46000000:base msacm32:0x42000000:acm)
set(base ntdll kernel32)
set(user ntdll kernel32 user32)
set(gdi ntdll kernel32 gdi32)
set(user_gdi ntdll kernel32 user32 gdi32)
set(crypt ntdll kernel32 crypt32)
set(acm ntdll kernel32 user32 gdi32 winmm)
set(objects_msi x86-lib-compound)
set(objects_winmm x86-lib-audio)
set(objects_gdiplus x86-lib-png)

if(MIME_GUEST_ARCH STREQUAL "x64")
    # vulkan-1, dxgi and direct3d for 32-bit programs on the linux host: the same sources with 32-bit
    # layouts, which speak the same wire.
    mime_dll(vulkan-1 x86 BASE 0x53000000 SOURCES ${MIME_VULKAN_SOURCES} ${MIME_VULKAN_GEN_SOURCES}
        OBJECTS x86-chkstk OPTIONS ${MIME_VULKAN_OPTIONS} INCLUDES ${MIME_VULKAN_INCLUDES}
        IMPORTS ntdll kernel32 win32u user32 DEPENDS mime-gen-vulkan)
    list(APPEND MIME_X86_DLLS dxgi:0x52000000:all_five d3d11:0x50000000:all_five d3dcompiler_47:0x4F000000:all_five
        d3d10core:0x4E000000:base d3d10_1core:0x4D000000:base d3d10:0x4C000000:all_five d3d10_1:0x4B000000:all_five
        d3d12:0x4A000000:all_five d3d9:0x49000000:all_five)
    set(objects_dxgi x86-lib-privdata)
    set(objects_d3d11 x86-lib-privdata x86-lib-dxbc)
    set(objects_d3dcompiler_47 x86-lib-dxbc:guest/lib/dxbc/container.c x86-lib-dxbc:guest/lib/dxbc/reader.c)
    set(objects_d3d10 x86-lib-dxbc:guest/lib/dxbc/container.c)
    set(objects_d3d10_1 x86-d3d10:guest/d3d10/create.c)
    set(objects_d3d10_1core x86-d3d10core:guest/d3d10core/kmt.c)
    set(objects_d3d12 x86-lib-privdata x86-lib-dxbc x86-lib-dxil x86-d3d11:guest/d3d11/format.c)
    set(objects_d3d9 x86-lib-privdata x86-lib-dxbc:guest/lib/dxbc/container.c)
    set(includes_dxgi third_party/khronos)
    set(includes_d3d11 third_party/khronos)
    set(includes_d3d12 third_party/khronos third_party/SPIRV-Cross)
endif()

foreach(entry IN LISTS MIME_X86_DLLS)
    string(REPLACE ":" ";" entry ${entry})
    list(GET entry 0 name)
    list(GET entry 1 address)
    list(GET entry 2 imports)
    mime_sources(sources ${name} x86)
    mime_dll(${name} x86 BASE ${address} SOURCES ${sources} OBJECTS x86-chkstk ${objects_${name}}
        INCLUDES ${includes_${name}} IMPORTS ${${imports}})
endforeach()

# winspool is a driver file.
mime_glob(sources guest/winspool/*.c)
mime_dll(winspool x86 BASE 0x55000000 FILE winspool.drv SOURCES ${sources} OBJECTS x86-chkstk
    IMPORTS ntdll kernel32 user32 gdi32)

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

# the acm codecs of 32-bit programs.
set(MIME_X86_ACM_CODECS msg711:0x41000000 imaadp32:0x40000000 msadp32:0x3F000000 msgsm32:0x3E000000)
foreach(entry IN LISTS MIME_X86_ACM_CODECS)
    string(REPLACE ":" ";" entry ${entry})
    list(GET entry 0 name)
    list(GET entry 1 base)
    mime_sources(sources ${name} x86)
    mime_dll(${name} x86 BASE ${base} FILE ${name}.acm SOURCES ${sources} OBJECTS x86-chkstk INCLUDES guest/lib
        IMPORTS ntdll kernel32 user32)
endforeach()
