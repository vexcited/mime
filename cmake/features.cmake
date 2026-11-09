# OptionalFeatures.exe, which turns the features that are fetched from the vendor on and off, and asks for them when a
# program needs one. It is in System32 under three names: dism.exe and fondue.exe are the same program, the names
# that scripts and programs written for Windows call. There is no 32-bit build: it writes the machine's registry and
# drive, and a 32-bit process would see both through the redirections of WOW64.
set(features_arch ${MIME_GUEST_ARCH})
mime_glob(features_sources guest/features/*.c guest/lib/features/*.c)
mime_objects(${features_arch}-features ${features_arch} SOURCES ${features_sources}
    INCLUDES guest/toolutil guest/features guest/lib/features)
set(features_libs ${MIME_LIB_${features_arch}}/ntdll.lib ${MIME_LIB_${features_arch}}/kernel32.lib
    ${MIME_LIB_${features_arch}}/advapi32.lib ${MIME_LIB_${features_arch}}/user32.lib
    ${MIME_LIB_${features_arch}}/comctl32.lib ${MIME_LIB_${features_arch}}/gdi32.lib ${MIME_LIB_${features_arch}}/winhttp.lib
    ${MIME_LIB_${features_arch}}/wintrust.lib ${MIME_LIB_${features_arch}}/crypt32.lib
    ${MIME_LIB_${features_arch}}/msi.lib)
foreach(features_name OptionalFeatures dism fondue)
    mime_link(${MIME_OUT_${features_arch}}/${features_name}.exe ${features_arch}
        OBJECTS ${features_arch}-features ${features_arch}-toolutil ${features_arch}-chkstk ${features_arch}-lib-cbs ${features_arch}-lib-msdelta
            ${features_arch}-lib-cabinet ${features_arch}-lib-png ${features_arch}-lib-lzms ${features_arch}-lib-crypto:guest/lib/crypto/hash.c LIBS ${features_libs}
        FLAGS /subsystem:console,10.0 /entry:entry /base:0x7FF6AE000000 /highentropyva /dynamicbase
            /stack:0x100000,0x4000
        ROM ${MIME_ROM_${features_arch}}/${features_name}.exe GROUPS guest)
endforeach()
