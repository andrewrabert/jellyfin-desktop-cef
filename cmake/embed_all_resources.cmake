# Generate C++ header with all embedded resources as byte arrays
# Called with:
#   -DRESOURCE_DIR=path/to/src/web
#   -DOUTPUT_FILE=path/to/generated/embedded_resources.h
#   -DBASE_URL=resources

function(get_mime_type EXT RESULT)
    if(EXT STREQUAL ".html")
        set(${RESULT} "text/html" PARENT_SCOPE)
    elseif(EXT STREQUAL ".css")
        set(${RESULT} "text/css" PARENT_SCOPE)
    elseif(EXT STREQUAL ".js")
        set(${RESULT} "application/javascript" PARENT_SCOPE)
    elseif(EXT STREQUAL ".png")
        set(${RESULT} "image/png" PARENT_SCOPE)
    elseif(EXT STREQUAL ".jpg" OR EXT STREQUAL ".jpeg")
        set(${RESULT} "image/jpeg" PARENT_SCOPE)
    elseif(EXT STREQUAL ".svg")
        set(${RESULT} "image/svg+xml" PARENT_SCOPE)
    elseif(EXT STREQUAL ".json")
        set(${RESULT} "application/json" PARENT_SCOPE)
    elseif(EXT STREQUAL ".woff2")
        set(${RESULT} "font/woff2" PARENT_SCOPE)
    elseif(EXT STREQUAL ".woff")
        set(${RESULT} "font/woff" PARENT_SCOPE)
    elseif(EXT STREQUAL ".ttf")
        set(${RESULT} "font/ttf" PARENT_SCOPE)
    else()
        set(${RESULT} "application/octet-stream" PARENT_SCOPE)
    endif()
endfunction()

file(GLOB_RECURSE ALL_FILES "${RESOURCE_DIR}/*")

set(CONTENT "#pragma once\n")
string(APPEND CONTENT "#include <cstdint>\n")
string(APPEND CONTENT "#include <cstddef>\n")
string(APPEND CONTENT "#include <string>\n")
string(APPEND CONTENT "#include <unordered_map>\n\n")

string(APPEND CONTENT "struct EmbeddedResource {\n")
string(APPEND CONTENT "    const uint8_t* data;\n")
string(APPEND CONTENT "    size_t size;\n")
string(APPEND CONTENT "    const char* mime_type;\n")
string(APPEND CONTENT "};\n\n")

set(INDEX 0)
set(MAP_ENTRIES "")

foreach(FILEPATH ${ALL_FILES})
    file(RELATIVE_PATH REL_PATH "${RESOURCE_DIR}" "${FILEPATH}")
    file(READ "${FILEPATH}" FILE_HEX HEX)
    file(SIZE "${FILEPATH}" FILE_SIZE)

    # Convert hex to comma-separated 0xNN bytes
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," HEX_BYTES "${FILE_HEX}")

    get_filename_component(EXT "${FILEPATH}" LAST_EXT)
    get_mime_type("${EXT}" MIME_TYPE)

    string(APPEND CONTENT "static const uint8_t _res_${INDEX}[] = {${HEX_BYTES}};\n")
    string(APPEND MAP_ENTRIES "    {\"${BASE_URL}/${REL_PATH}\", {_res_${INDEX}, ${FILE_SIZE}, \"${MIME_TYPE}\"}},\n")

    math(EXPR INDEX "${INDEX} + 1")
endforeach()

string(APPEND CONTENT "\ninline const std::unordered_map<std::string, EmbeddedResource> embedded_resources = {\n")
string(APPEND CONTENT "${MAP_ENTRIES}")
string(APPEND CONTENT "};\n")

file(WRITE "${OUTPUT_FILE}" "${CONTENT}")
