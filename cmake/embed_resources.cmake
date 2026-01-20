# Generate C++ header with embedded file contents as a map
# Called with -DJS_FILES="file1;file2;..." -DOUTPUT_FILE=output.h

# Convert space-separated command-line arg back to CMake list
string(REPLACE " " ";" JS_FILE_LIST "${JS_FILES}")

set(CONTENT "#pragma once\n#include <string>\n#include <unordered_map>\n\n")

# Generate byte arrays for each file
foreach(INPUT_FILE ${JS_FILE_LIST})
    get_filename_component(FILENAME ${INPUT_FILE} NAME)
    string(MAKE_C_IDENTIFIER "${FILENAME}" VAR_NAME)
    file(READ ${INPUT_FILE} FILE_CONTENT)
    string(LENGTH "${FILE_CONTENT}" FILE_LEN)

    # Convert to hex bytes
    string(HEX "${FILE_CONTENT}" HEX_CONTENT)
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," HEX_BYTES "${HEX_CONTENT}")

    string(APPEND CONTENT "static const unsigned char ${VAR_NAME}_data[] = {\n    ${HEX_BYTES}0x00\n};\n\n")
endforeach()

# Generate the map
string(APPEND CONTENT "inline const std::unordered_map<std::string, const char*> embedded_js = {\n")

set(FIRST TRUE)
foreach(INPUT_FILE ${JS_FILE_LIST})
    get_filename_component(FILENAME ${INPUT_FILE} NAME)
    string(MAKE_C_IDENTIFIER "${FILENAME}" VAR_NAME)
    if(NOT FIRST)
        string(APPEND CONTENT ",\n")
    endif()
    set(FIRST FALSE)
    string(APPEND CONTENT "    {\"${FILENAME}\", reinterpret_cast<const char*>(${VAR_NAME}_data)}")
endforeach()

string(APPEND CONTENT "\n};\n")
file(WRITE ${OUTPUT_FILE} "${CONTENT}")
