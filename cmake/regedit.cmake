# regedit.exe and reg.exe, and the reader and writer of .reg files that both are built on.
# regedit is at C:\Windows\regedit.exe and, for the 32 bit one, C:\Windows\SysWOW64\regedit.exe; reg is in the system folders.
set(MIME_ROM_WINDOWS ${MIME_ROM}/winroot)

mime_glob(regfile_sources guest/lib/regfile/*.c)
mime_glob(regedit_sources guest/regedit/*.c)
mime_glob(reg_sources guest/reg/*.c)

foreach(arch IN LISTS MIME_USER_ARCHS ITEMS x86)
    if(arch STREQUAL "x86" OR arch STREQUAL MIME_GUEST_ARCH)
        set(regedit_rom ${MIME_ROM_${arch}})
        if(arch STREQUAL MIME_GUEST_ARCH)
            set(regedit_rom ${MIME_ROM_WINDOWS})
        endif()
        mime_objects(${arch}-regfile ${arch} SOURCES ${regfile_sources} INCLUDES guest/lib/regfile)

        mime_objects(${arch}-regedit ${arch} SOURCES ${regedit_sources} INCLUDES guest/regedit guest/lib/regfile)
        set(regedit_libs ${MIME_LIB_${arch}}/ntdll.lib ${MIME_LIB_${arch}}/kernel32.lib ${MIME_LIB_${arch}}/user32.lib
            ${MIME_LIB_${arch}}/gdi32.lib ${MIME_LIB_${arch}}/advapi32.lib ${MIME_LIB_${arch}}/comctl32.lib
            ${MIME_LIB_${arch}}/comdlg32.lib ${MIME_LIB_${arch}}/shell32.lib)
        set(regedit_flags /subsystem:windows,10.0 /entry:entry /base:0x7FF6C4000000 /highentropyva /dynamicbase
            /stack:0x100000,0x4000)
        if(arch STREQUAL "x86")
            set(regedit_flags /subsystem:windows,6.0 /entry:entry /base:0x00400000 /dynamicbase /stack:0x100000,0x4000)
        endif()
        mime_link(${MIME_OUT_${arch}}/regedit.exe ${arch} OBJECTS ${arch}-regedit ${arch}-regfile ${arch}-chkstk
            LIBS ${regedit_libs} FLAGS ${regedit_flags} RESOURCE regedit.exe
            RESOURCE_SCRIPT ${CMAKE_SOURCE_DIR}/guest/regedit/regedit.rc ROM ${regedit_rom}/regedit.exe GROUPS guest)

        mime_objects(${arch}-reg ${arch} SOURCES ${reg_sources} INCLUDES guest/reg guest/lib/regfile)
        set(reg_libs ${MIME_LIB_${arch}}/ntdll.lib ${MIME_LIB_${arch}}/kernel32.lib ${MIME_LIB_${arch}}/advapi32.lib)
        set(reg_flags /subsystem:console,10.0 /entry:entry /base:0x7FF6C5000000 /highentropyva /dynamicbase
            /stack:0x100000,0x4000)
        if(arch STREQUAL "x86")
            set(reg_flags /subsystem:console,6.0 /entry:entry /base:0x00400000 /dynamicbase /stack:0x100000,0x4000)
        endif()
        mime_link(${MIME_OUT_${arch}}/reg.exe ${arch} OBJECTS ${arch}-reg ${arch}-regfile ${arch}-chkstk LIBS ${reg_libs}
            FLAGS ${reg_flags} RESOURCE reg.exe RESOURCE_SCRIPT ${CMAKE_SOURCE_DIR}/guest/reg/reg.rc
            ROM ${MIME_ROM_${arch}}/reg.exe GROUPS guest)
    endif()
endforeach()
