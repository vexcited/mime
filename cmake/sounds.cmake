# The system sounds Mime makes, served from the ROM as the files of C:\Windows\Media, and the General MIDI sound set the
# wavetable synth plays, served as C:\Windows\System32\drivers\gm.dls.
file(GLOB mime_rom_sounds CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/data/winmedia/*.wav)
set_property(GLOBAL APPEND PROPERTY MIME_ROM_FILES ${mime_rom_sounds} ${CMAKE_SOURCE_DIR}/data/windrivers/gm.dls)
