# Generate C++ header with embedded file contents as a map
# Called with -DJS_FILES="file1;file2;..." -DOUTPUT_FILE=output.h

# Convert space-separated command-line arg back to CMake list
string(REPLACE " " ";" JS_FILE_LIST "${JS_FILES}")

set(CONTENT "#pragma once\n#include <string>\n#include <unordered_map>\n\n")
string(APPEND CONTENT "inline const std::unordered_map<std::string, const char*> embedded_js = {\n")

set(FIRST TRUE)
foreach(INPUT_FILE ${JS_FILE_LIST})
    get_filename_component(FILENAME ${INPUT_FILE} NAME)
    file(READ ${INPUT_FILE} FILE_CONTENT)
    if(NOT FIRST)
        string(APPEND CONTENT ",\n")
    endif()
    set(FIRST FALSE)
    string(APPEND CONTENT "    {\"${FILENAME}\", R\"JS(\n${FILE_CONTENT})JS\"}")
endforeach()

string(APPEND CONTENT "\n};\n")
file(WRITE ${OUTPUT_FILE} "${CONTENT}")
