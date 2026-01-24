# Generate version.h at build time
# Called via: cmake -P GenerateVersion.cmake

set(VERSION_FILE "${BINARY_DIR}/src/version.h")
set(VERSION_CACHE "${BINARY_DIR}/src/.version_cache")
set(HASH_CACHE "${BINARY_DIR}/src/.git_hash_cache")
set(EPOCH_CACHE "${BINARY_DIR}/src/.epoch_cache")

# Read current values
if(EXISTS "${SOURCE_DIR}/VERSION")
    file(READ "${SOURCE_DIR}/VERSION" APP_VERSION)
    string(STRIP "${APP_VERSION}" APP_VERSION)
else()
    set(APP_VERSION "unknown")
endif()

execute_process(
    COMMAND git describe --always --dirty
    WORKING_DIRECTORY "${SOURCE_DIR}"
    OUTPUT_VARIABLE APP_GIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE GIT_RESULT
)
if(NOT GIT_RESULT EQUAL 0)
    set(APP_GIT_HASH "")
endif()

set(SOURCE_EPOCH "$ENV{SOURCE_DATE_EPOCH}")

# Read cached values
set(CACHED_VERSION "")
set(CACHED_HASH "")
set(CACHED_EPOCH "")
if(EXISTS "${VERSION_CACHE}")
    file(READ "${VERSION_CACHE}" CACHED_VERSION)
endif()
if(EXISTS "${HASH_CACHE}")
    file(READ "${HASH_CACHE}" CACHED_HASH)
endif()
if(EXISTS "${EPOCH_CACHE}")
    file(READ "${EPOCH_CACHE}" CACHED_EPOCH)
endif()

# Update if changed
if(NOT "${APP_VERSION}" STREQUAL "${CACHED_VERSION}" OR
   NOT "${APP_GIT_HASH}" STREQUAL "${CACHED_HASH}" OR
   NOT "${SOURCE_EPOCH}" STREQUAL "${CACHED_EPOCH}")
    file(WRITE "${VERSION_CACHE}" "${APP_VERSION}")
    file(WRITE "${HASH_CACHE}" "${APP_GIT_HASH}")
    file(WRITE "${EPOCH_CACHE}" "${SOURCE_EPOCH}")
    file(WRITE "${VERSION_FILE}"
"#pragma once

#define APP_VERSION \"${APP_VERSION}\"
#define APP_GIT_HASH \"${APP_GIT_HASH}\"
")
    message(STATUS "version.h updated: ${APP_VERSION} (${APP_GIT_HASH})")
endif()
