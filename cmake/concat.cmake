set(text "")
foreach(input IN LISTS INPUTS)
    file(READ ${input} part)
    string(APPEND text "${part}")
endforeach()
# a script names its files from the root of the source tree.
string(REPLACE "@MIME_ROOT@" "${ROOT}" text "${text}")
file(WRITE ${OUTPUT} "${text}")
