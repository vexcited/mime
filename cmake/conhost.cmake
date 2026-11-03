# conhost.exe, the program that draws the window of a console: a graphical program of System32.
mime_glob(conhost_sources guest/conhost/*.c)
foreach(arch IN LISTS MIME_USER_ARCHS)
    if(arch STREQUAL MIME_GUEST_ARCH)
        mime_objects(${arch}-conhost ${arch} SOURCES ${conhost_sources} INCLUDES guest/conhost)
        set(conhost_libs ${MIME_LIB_${arch}}/ntdll.lib ${MIME_LIB_${arch}}/kernel32.lib ${MIME_LIB_${arch}}/user32.lib
            ${MIME_LIB_${arch}}/gdi32.lib ${MIME_LIB_${arch}}/advapi32.lib)
        set(conhost_flags /subsystem:windows,10.0 /entry:entry /base:0x7FF6C6000000 /highentropyva /dynamicbase
            /stack:0x100000,0x4000)
        mime_link(${MIME_OUT_${arch}}/conhost.exe ${arch} OBJECTS ${arch}-conhost ${arch}-chkstk LIBS ${conhost_libs}
            FLAGS ${conhost_flags} RESOURCE conhost.exe RESOURCE_SCRIPT ${CMAKE_SOURCE_DIR}/guest/conhost/conhost.rc
            ROM ${MIME_ROM_${arch}}/conhost.exe GROUPS guest)
    endif()
endforeach()
