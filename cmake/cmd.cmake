# cmd.exe, the command interpreter: a console program of System32 and SysWOW64.
mime_glob(cmd_sources guest/cmd/*.c)
foreach(arch IN LISTS MIME_USER_ARCHS ITEMS x86)
    if(arch STREQUAL "x86" OR arch STREQUAL MIME_GUEST_ARCH)
        mime_objects(${arch}-cmd ${arch} SOURCES ${cmd_sources} INCLUDES guest/cmd)
        set(cmd_libs ${MIME_LIB_${arch}}/ntdll.lib ${MIME_LIB_${arch}}/kernel32.lib)
        set(cmd_objects ${arch}-cmd ${arch}-chkstk)
        set(cmd_flags /subsystem:console,10.0 /entry:entry /base:0x7FF6AD000000 /highentropyva /dynamicbase
            /stack:0x1000000,0x10000)
        if(arch STREQUAL "x86")
            list(APPEND cmd_objects x86-div)
            set(cmd_flags /subsystem:console,6.0 /entry:entry /base:0x00400000 /dynamicbase /stack:0x1000000,0x10000)
        endif()
        mime_link(${MIME_OUT_${arch}}/cmd.exe ${arch} OBJECTS ${cmd_objects} LIBS ${cmd_libs} FLAGS ${cmd_flags}
            RESOURCE cmd.exe ROM ${MIME_ROM_${arch}}/cmd.exe GROUPS guest)
    endif()
endforeach()
