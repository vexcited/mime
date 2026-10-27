# The system sounds Mime makes, served from the ROM as the files of C:\Windows\Media.
file(GLOB mime_rom_sounds CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/data/winmedia/*.wav)
set_property(GLOBAL APPEND PROPERTY MIME_ROM_FILES ${mime_rom_sounds})
