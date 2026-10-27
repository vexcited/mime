# the visual style of the machine, drawn by tools/gentheme.py of the research repository and committed in gen/themes, carried in the
# ROM as Windows\Resources\Themes\aero\aero.msstyles, see cmake/rom.cmake.
set_property(GLOBAL APPEND PROPERTY MIME_ROM_FILES ${CMAKE_SOURCE_DIR}/gen/themes/aero/aero.msstyles)
