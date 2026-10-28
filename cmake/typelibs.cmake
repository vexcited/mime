# The automation type library every other library imports, as Windows ships it: the 64-bit one as
# C:\Windows\System32\stdole2.tlb and the 32-bit one as C:\Windows\SysWOW64\stdole2.tlb.
set_property(GLOBAL APPEND PROPERTY MIME_ROM_FILES ${CMAKE_SOURCE_DIR}/data/wintypelibs/stdole2.tlb
    ${CMAKE_SOURCE_DIR}/data/syswow64/stdole2.tlb)
