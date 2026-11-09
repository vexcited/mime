# the sRGB profile of the color folder, made by cmake/asset/icc.cmake, served from the ROM as
# C:\Windows\System32\spool\drivers\color\sRGB Color Space Profile.icm.
set(mime_srgb_profile "${MIME_ASSETS}/wincolor/sRGB Color Space Profile.icm")
add_custom_command(OUTPUT "${mime_srgb_profile}"
    COMMAND ${CMAKE_COMMAND} "-DOUTPUT=${mime_srgb_profile}" -P ${CMAKE_SOURCE_DIR}/cmake/asset/icc.cmake
    DEPENDS ${CMAKE_SOURCE_DIR}/cmake/asset/icc.cmake ${CMAKE_SOURCE_DIR}/cmake/asset/binary.cmake VERBATIM)
set_property(GLOBAL APPEND PROPERTY MIME_ROM_FILES "${mime_srgb_profile}")
