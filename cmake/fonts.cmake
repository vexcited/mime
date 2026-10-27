# The open font that stands in when a requested face is missing, served from the ROM as files of C:\Windows\Fonts.
# It keeps its own name, Noto Sans, and the license text goes along with it.
file(GLOB mime_rom_fonts ${CMAKE_SOURCE_DIR}/third_party/fonts/winfonts/*)
set_property(GLOBAL APPEND PROPERTY MIME_ROM_FILES ${mime_rom_fonts})
