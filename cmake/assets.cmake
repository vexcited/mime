# The drawings and sounds that live in their own repositories, mime-icons and mime-audio, and the files of Windows the
# build makes from them and from data/messages. A checkout of a repository next to Mime's is used as it is, so it can be
# edited and built without a commit; otherwise the commit pinned here is fetched. MIME_ICONS_DIR and MIME_AUDIO_DIR
# name another checkout.
include(FetchContent)

set(MIME_ICONS_REPOSITORY ssh://git@code.vexcited.com/mime/icons.git)
set(MIME_ICONS_COMMIT dd61167f31ec0551f02ee18716479f50ea62d681)
set(MIME_AUDIO_REPOSITORY ssh://git@code.vexcited.com/mime/audio.git)
set(MIME_AUDIO_COMMIT 86c3138fcab689175f18847b72b06820c7dd0bd6)
set(MIME_ICONS_DIR "" CACHE PATH "A checkout of mime-icons to build from instead of the pinned commit")
set(MIME_AUDIO_DIR "" CACHE PATH "A checkout of mime-audio to build from instead of the pinned commit")

set(MIME_ASSETS ${MIME_BUILD_GEN}/assets)
set(MIME_ASSET_SCRIPTS ${CMAKE_SOURCE_DIR}/cmake/asset/binary.cmake ${CMAKE_SOURCE_DIR}/cmake/asset/raster.cmake
    ${CMAKE_SOURCE_DIR}/cmake/asset/png.cmake)

function(mime_asset_repository out name url commit)
    string(TOUPPER "${name}" upper)
    set(dir "${MIME_${upper}_DIR}")
    if(NOT dir)
        # next to the checkout, or next to the main checkout of a worktree.
        set(roots ${CMAKE_SOURCE_DIR})
        execute_process(COMMAND git rev-parse --path-format=absolute --git-common-dir
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} OUTPUT_VARIABLE common OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE failed ERROR_QUIET)
        if(NOT failed)
            get_filename_component(main "${common}" DIRECTORY)
            list(APPEND roots "${main}")
        endif()
        foreach(root IN LISTS roots)
            get_filename_component(candidate "${root}/../mime-${name}" ABSOLUTE)
            if(IS_DIRECTORY "${candidate}")
                set(dir "${candidate}")
                break()
            endif()
        endforeach()
    endif()
    if(dir)
        message(STATUS "mime-${name}: ${dir}")
    else()
        FetchContent_Declare(mime_${name} GIT_REPOSITORY ${url} GIT_TAG ${commit} GIT_PROGRESS TRUE)
        FetchContent_MakeAvailable(mime_${name})
        set(dir "${mime_${name}_SOURCE_DIR}")
        message(STATUS "mime-${name}: ${commit}")
    endif()
    set(${out} "${dir}" PARENT_SCOPE)
endfunction()

mime_asset_repository(MIME_ICONS icons ${MIME_ICONS_REPOSITORY} ${MIME_ICONS_COMMIT})
mime_asset_repository(MIME_AUDIO audio ${MIME_AUDIO_REPOSITORY} ${MIME_AUDIO_COMMIT})

set(mime_icon_sizes 16 20 24 32 48 256)
set(mime_system_icon_sizes 16 20 24 32 40 48 64 256)
set(mime_cursor_sizes 32 48 64 96 128)
set(mime_messages_kernelbase win32 hresult)
set(mime_messages_ntdll ntstatus)

function(_mime_icon_command output kind images depends)
    add_custom_command(OUTPUT ${output}
        COMMAND ${CMAKE_COMMAND} -DKIND=${kind} -DOUTPUT=${output} "-DIMAGES=${images}" ${ARGN}
            -P ${CMAKE_SOURCE_DIR}/cmake/asset/icon.cmake
        DEPENDS ${depends} ${CMAKE_SOURCE_DIR}/cmake/asset/icon.cmake ${MIME_ASSET_SCRIPTS} VERBATIM)
endfunction()

function(_mime_sized out svg sizes)
    set(images)
    foreach(size IN LISTS sizes)
        list(APPEND images "${svg}@${size}")
    endforeach()
    set(${out} "${images}" PARENT_SCOPE)
endfunction()

# The file a resource script names as @MIME_ASSETS@/<path>, and the rule that makes it.
function(mime_asset path)
    get_property(done GLOBAL PROPERTY MIME_ASSET_${path})
    if(done)
        return()
    endif()
    set_property(GLOBAL PROPERTY MIME_ASSET_${path} TRUE)
    set(output ${MIME_ASSETS}/${path})
    set(art ${MIME_ICONS})
    if(path MATCHES "^icons/system_(.+)\\.ico$")
        set(svg ${art}/icons/${CMAKE_MATCH_1}.svg)
        _mime_sized(images ${svg} "${mime_system_icon_sizes}")
        _mime_icon_command(${output} ico "${images}" ${svg})
    elseif(path MATCHES "^icons/(.+)\\.ico$")
        set(svg ${art}/icons/${CMAKE_MATCH_1}.svg)
        _mime_sized(images ${svg} "${mime_icon_sizes}")
        _mime_icon_command(${output} ico "${images}" ${svg})
    elseif(path MATCHES "^cursors/(.+)\\.cur$")
        set(svg ${art}/cursors/${CMAKE_MATCH_1}.svg)
        _mime_sized(images ${svg} "${mime_cursor_sizes}")
        _mime_icon_command(${output} cur "${images}" ${svg})
    elseif(path MATCHES "^cursors/(.+)\\.ani$")
        # a frame is a cursor of its own, made in parallel with the others.
        set(name ${CMAKE_MATCH_1})
        file(GLOB frames CONFIGURE_DEPENDS ${art}/cursors/${name}_[0-9][0-9].svg)
        list(SORT frames)
        if(NOT frames)
            message(FATAL_ERROR "mime-icons has no frames of the cursor ${name}")
        endif()
        set(curs)
        foreach(svg IN LISTS frames)
            get_filename_component(frame ${svg} NAME_WE)
            set(cur ${MIME_ASSETS}/cursors/frames/${frame}.cur)
            _mime_sized(images ${svg} "${mime_cursor_sizes}")
            _mime_icon_command(${cur} cur "${images}" ${svg})
            list(APPEND curs ${cur})
        endforeach()
        list(GET frames 0 first)
        _mime_icon_command(${output} ani "${curs}" "${curs}" -DJIFFIES_FROM=${first})
    elseif(path MATCHES "^bitmaps/(.+)\\.bmp$")
        set(svg ${art}/bitmaps/${CMAKE_MATCH_1}.svg)
        _mime_icon_command(${output} bmp ${svg} ${svg})
    elseif(path MATCHES "^programs/(.+)/(.+)\\.ico$")
        file(GLOB images CONFIGURE_DEPENDS ${art}/programs/${CMAKE_MATCH_1}/${CMAKE_MATCH_2}_*.svg)
        if(NOT images)
            message(FATAL_ERROR "mime-icons has no drawing of ${path}")
        endif()
        _mime_icon_command(${output} ico "${images}" "${images}")
    elseif(path MATCHES "^messages/(.+)\\.bin$")
        set(inputs)
        foreach(list IN LISTS mime_messages_${CMAKE_MATCH_1})
            list(APPEND inputs ${CMAKE_SOURCE_DIR}/data/messages/${list}.txt)
        endforeach()
        add_custom_command(OUTPUT ${output}
            COMMAND ${CMAKE_COMMAND} -DOUTPUT=${output} "-DINPUTS=${inputs}"
                -P ${CMAKE_SOURCE_DIR}/cmake/asset/messages.cmake
            DEPENDS ${inputs} ${CMAKE_SOURCE_DIR}/cmake/asset/messages.cmake
                ${CMAKE_SOURCE_DIR}/cmake/asset/binary.cmake VERBATIM)
    else()
        message(FATAL_ERROR "no rule makes the asset ${path}")
    endif()
endfunction()

# The assets the resource scripts of inputs name, made known to the build, into out.
function(mime_script_assets out)
    set(outputs)
    foreach(input IN LISTS ARGN)
        file(STRINGS ${input} lines REGEX "@MIME_ASSETS@/")
        foreach(line IN LISTS lines)
            string(REGEX MATCHALL "@MIME_ASSETS@/[^\"]+" paths "${line}")
            foreach(path IN LISTS paths)
                string(REPLACE "@MIME_ASSETS@/" "" path "${path}")
                mime_asset(${path})
                list(APPEND outputs ${MIME_ASSETS}/${path})
            endforeach()
        endforeach()
    endforeach()
    set(${out} ${outputs} PARENT_SCOPE)
endfunction()

# The system sounds, C:\Windows\Media, and the General MIDI set, C:\Windows\System32\drivers\gm.dls, for the ROM.
file(GLOB mime_sound_recipes CONFIGURE_DEPENDS ${MIME_AUDIO}/sounds/*.txt)
foreach(recipe IN LISTS mime_sound_recipes)
    get_filename_component(name "${recipe}" NAME_WE)
    set(output "${MIME_ASSETS}/winmedia/${name}.wav")
    add_custom_command(OUTPUT "${output}"
        COMMAND ${CMAKE_COMMAND} "-DINPUT=${recipe}" "-DOUTPUT=${output}" -P ${CMAKE_SOURCE_DIR}/cmake/asset/wave.cmake
        DEPENDS "${recipe}" ${CMAKE_SOURCE_DIR}/cmake/asset/wave.cmake ${MIME_ASSET_SCRIPTS} VERBATIM)
    set_property(GLOBAL APPEND PROPERTY MIME_ROM_FILES "${output}")
endforeach()
add_custom_command(OUTPUT ${MIME_ASSETS}/windrivers/gm.dls
    COMMAND ${CMAKE_COMMAND} -E copy ${MIME_AUDIO}/soundsets/gm.dls ${MIME_ASSETS}/windrivers/gm.dls
    DEPENDS ${MIME_AUDIO}/soundsets/gm.dls VERBATIM)
set_property(GLOBAL APPEND PROPERTY MIME_ROM_FILES ${MIME_ASSETS}/windrivers/gm.dls)
