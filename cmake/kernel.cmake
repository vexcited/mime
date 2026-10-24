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
