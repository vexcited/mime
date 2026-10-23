# concatenates INPUTS into OUTPUT.
set(text "")
foreach(input IN LISTS INPUTS)
    file(READ ${input} part)
    string(APPEND text "${part}")
endforeach()
file(WRITE ${OUTPUT} "${text}")
