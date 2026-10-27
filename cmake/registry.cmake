# the registry hives of a new machine, compiled from the sources of data/registry and carried in the ROM. the kernel loads
# them at boot, see guest/ntos/cm/registry.c.
add_executable(regc host/regc/regc.c)
target_compile_options(regc PRIVATE -Wall -Wextra -Wimplicit-fallthrough)
set_target_properties(regc PROPERTIES C_STANDARD 23 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tools)

mime_glob(registry_sources data/registry/*.reg)
set(MIME_HIVES ${CMAKE_BINARY_DIR}/guest/hives)
set(registry_hives ${MIME_HIVES}/SYSTEM ${MIME_HIVES}/SOFTWARE ${MIME_HIVES}/DEFAULT ${MIME_HIVES}/NTUSER.DAT)
add_custom_command(OUTPUT ${registry_hives}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${MIME_HIVES}
    COMMAND regc -o ${MIME_HIVES} ${registry_sources}
    DEPENDS regc ${registry_sources} COMMENT "Compiling the registry hives" VERBATIM)
set_property(GLOBAL APPEND PROPERTY MIME_ROM_FILES ${registry_hives})
