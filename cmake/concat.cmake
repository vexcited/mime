# writes OUTPUT with the contents of the files of INPUTS, one after the other.
set(text "")
foreach(input IN LISTS INPUTS)
    file(READ ${input} part)
    string(APPEND text "${part}")
endforeach()
file(WRITE ${OUTPUT} "${text}")
