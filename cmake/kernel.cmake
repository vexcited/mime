# ntoskrnl.exe and win32k.sys, built for the machine's processor.

set(ntos_options -DMIME_KERNEL=1)
mime_glob(rtl_sources guest/rtl/*.c)
mime_glob(crt_sources guest/crt/*.c)
if(MIME_GUEST_ARCH STREQUAL "x64")
    mime_glob(crt_kernel_asm guest/crt/x64/*.S)
else()
    mime_glob(crt_kernel_asm guest/crt/*.S)
endif()
mime_glob(ntos_asm guest/ntos/arch/${MIME_GUEST_ARCH}/*.S)

mime_objects(kernel-ntos kernel SOURCES ${MIME_NTOS_SOURCES} ${ntos_asm} OPTIONS ${ntos_options}
    INCLUDES guest/ntos)
mime_objects(kernel-rtl kernel SOURCES ${rtl_sources} OPTIONS ${ntos_options} INCLUDES guest/ntos)
mime_objects(kernel-crt kernel SOURCES ${crt_sources} ${crt_kernel_asm} OPTIONS -fno-builtin)
mime_objects(kernel-services kernel SOURCES ${MIME_GEN}/ntos_services.c OPTIONS ${ntos_options} -Wno-unused-function
    INCLUDES guest/ntos)

# exported: public kernel routines, the crt and the object types drivers use. the ones private to mime's own
# drivers go by ordinal only, so no Mime name shows in the export table.
mime_object_list(ntos_objects kernel-ntos kernel-rtl kernel-crt)
add_custom_command(OUTPUT ${MIME_GEN}/ntoskrnl.def
    COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/tools/gendef.py symbols ntoskrnl.exe ${MIME_GEN}/ntoskrnl.def
        --code "^(Nt|Zw|Ke|Ex|Io|Ob|Ps|Mm|Rtl|Hal|Dbg|vDbg|Se|Po|Kd|Fs|Cc|Wmi|Mime)[A-Z]|^(Obf|Iof|Kef|Kf|Exf)[A-Z]|^(_?str|_?wcs|mem|_vsn|_sn|sprintf|swprintf|to|tow|__chkstk)"
        --data "^(Ex|Io|Ps|Mm|Se|Ob)[A-Za-z]*(Type|TypeObject)$|^SharedUserData$|^HalBootInfo$"
        --private "^(Mime|HalMime)" ${ntos_objects}
    DEPENDS tools/gendef.py ${ntos_objects} kernel-ntos kernel-rtl kernel-crt COMMAND_EXPAND_LISTS VERBATIM)

set(MIME_NTOSKRNL_LIB ${MIME_LIB}/ntoskrnl.lib)
mime_link(${MIME_SYSTEM32}/ntoskrnl.exe ${MIME_GUEST_ARCH}
    OBJECTS kernel-ntos kernel-rtl kernel-crt kernel-services
    FLAGS /subsystem:native,10.0 /driver /entry:KiSystemStartup /base:0xFFFFF80000200000 /stack:0x80000,0x2000
        /def:${MIME_GEN}/ntoskrnl.def /map:${CMAKE_BINARY_DIR}/guest/ntoskrnl.map
    DEPENDS ${MIME_GEN}/ntoskrnl.def IMPLIB ${MIME_NTOSKRNL_LIB} RESOURCE ntoskrnl.exe GROUPS guest)

mime_glob(win32k_lib_sources guest/lib/ttf/*.c guest/lib/png/*.c guest/lib/msstyles/*.c)
mime_objects(kernel-win32k kernel SOURCES ${MIME_WIN32K_OWN_SOURCES} ${win32k_lib_sources} ${MIME_GEN}/win32k_services.c
    INCLUDES guest/win32k guest/lib third_party/khronos)
mime_link(${MIME_SYSTEM32}/win32k.sys ${MIME_GUEST_ARCH} OBJECTS kernel-win32k LIBS ${MIME_NTOSKRNL_LIB}
    FLAGS /subsystem:native,10.0 /driver /entry:DriverEntry /base:0xFFFFF80040000000 /stack:0x40000,0x1000
        /map:${CMAKE_BINARY_DIR}/guest/win32k.map
    RESOURCE win32k.sys GROUPS guest)

mime_rom_file(${MIME_SYSTEM32}/ntoskrnl.exe ${MIME_ROM}/ntoskrnl.exe)
mime_rom_file(${MIME_SYSTEM32}/win32k.sys ${MIME_ROM}/win32k.sys)
