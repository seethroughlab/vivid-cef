# vivid-cef — CEF browser operator package for Vivid
#
# Built by PackageManager with:
#   cmake -B <build> -S <pkg> -DVIVID_SRC_DIR=... -DVIVID_BUILD_DIR=... -DVIVID_PLUGIN_SUFFIX=...
#
# Produces:
#   browser${VIVID_PLUGIN_SUFFIX}  — the operator plugin
#   vivid-cef-helper               — CEF subprocess executable

cmake_minimum_required(VERSION 3.16)
project(vivid-cef)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT VIVID_SRC_DIR)
    message(FATAL_ERROR "VIVID_SRC_DIR not set — this package must be built via vivid's PackageManager")
endif()

# Default plugin suffix
if(NOT VIVID_PLUGIN_SUFFIX)
    if(APPLE)
        set(VIVID_PLUGIN_SUFFIX ".dylib")
    elseif(WIN32)
        set(VIVID_PLUGIN_SUFFIX ".dll")
    else()
        set(VIVID_PLUGIN_SUFFIX ".so")
    endif()
endif()

# =============================================================================
# CEF version and download
# =============================================================================

set(CEF_VERSION "120.2.7+g4bc6a59+chromium-120.0.6099.234")

if(WIN32)
    set(CEF_PLATFORM "windows64")
elseif(APPLE)
    execute_process(COMMAND uname -m OUTPUT_VARIABLE HOST_ARCH OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(HOST_ARCH STREQUAL "arm64")
        set(CEF_PLATFORM "macosarm64")
    else()
        set(CEF_PLATFORM "macosx64")
    endif()
else()
    set(CEF_PLATFORM "linux64")
endif()

string(REPLACE "+" "%2B" CEF_VERSION_ENCODED "${CEF_VERSION}")
set(CEF_URL "https://cef-builds.spotifycdn.com/cef_binary_${CEF_VERSION_ENCODED}_${CEF_PLATFORM}_minimal.tar.bz2")
set(CEF_DIR "${CMAKE_BINARY_DIR}/_deps/cef")
set(CEF_ARCHIVE "${CEF_DIR}/cef.tar.bz2")

file(MAKE_DIRECTORY "${CEF_DIR}")

# Check if already downloaded (minimal CEF > 50MB)
set(CEF_SKIP_DOWNLOAD FALSE)
if(EXISTS "${CEF_ARCHIVE}")
    file(SIZE "${CEF_ARCHIVE}" CEF_FILE_SIZE)
    if(CEF_FILE_SIZE GREATER 50000000)
        message(STATUS "[vivid-cef] Using cached CEF (${CEF_FILE_SIZE} bytes)")
        set(CEF_SKIP_DOWNLOAD TRUE)
    endif()
endif()

if(NOT CEF_SKIP_DOWNLOAD)
    message(STATUS "[vivid-cef] Downloading CEF from ${CEF_URL}")
    file(DOWNLOAD ${CEF_URL} "${CEF_ARCHIVE}"
        STATUS CEF_DOWNLOAD_STATUS TIMEOUT 600 SHOW_PROGRESS)
    list(GET CEF_DOWNLOAD_STATUS 0 CEF_DOWNLOAD_OK)
    list(GET CEF_DOWNLOAD_STATUS 1 CEF_DOWNLOAD_MSG)

    if(NOT CEF_DOWNLOAD_OK EQUAL 0)
        # Fallback: try curl
        find_program(CURL_EXECUTABLE curl)
        if(CURL_EXECUTABLE)
            execute_process(
                COMMAND ${CURL_EXECUTABLE} -fL --retry 3 -o "${CEF_ARCHIVE}" "${CEF_URL}"
                RESULT_VARIABLE CURL_RESULT)
            if(NOT CURL_RESULT EQUAL 0)
                message(FATAL_ERROR "[vivid-cef] Failed to download CEF")
            endif()
        else()
            message(FATAL_ERROR "[vivid-cef] Download failed: ${CEF_DOWNLOAD_MSG}")
        endif()
    endif()
endif()

# Extract
set(CEF_EXTRACT_DIR "${CEF_DIR}/extracted")
if(NOT EXISTS "${CEF_EXTRACT_DIR}")
    message(STATUS "[vivid-cef] Extracting CEF...")
    file(MAKE_DIRECTORY "${CEF_EXTRACT_DIR}")
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E tar xjf "${CEF_ARCHIVE}"
        WORKING_DIRECTORY "${CEF_EXTRACT_DIR}"
        RESULT_VARIABLE EXTRACT_RESULT)
    if(NOT EXTRACT_RESULT EQUAL 0)
        message(FATAL_ERROR "[vivid-cef] Failed to extract CEF")
    endif()
endif()

file(GLOB CEF_ROOT_DIRS "${CEF_EXTRACT_DIR}/cef_binary_*")
list(GET CEF_ROOT_DIRS 0 CEF_ROOT)
if(NOT CEF_ROOT OR NOT EXISTS "${CEF_ROOT}")
    message(FATAL_ERROR "[vivid-cef] Could not find extracted CEF in ${CEF_EXTRACT_DIR}")
endif()
message(STATUS "[vivid-cef] CEF root: ${CEF_ROOT}")

# =============================================================================
# cef_dll_wrapper static library
# =============================================================================

set(CEF_WRAPPER_SOURCES
    ${CEF_ROOT}/libcef_dll/wrapper/cef_byte_read_handler.cc
    ${CEF_ROOT}/libcef_dll/wrapper/cef_closure_task.cc
    ${CEF_ROOT}/libcef_dll/wrapper/cef_message_router.cc
    ${CEF_ROOT}/libcef_dll/wrapper/cef_message_router_utils.cc
    ${CEF_ROOT}/libcef_dll/wrapper/cef_resource_manager.cc
    ${CEF_ROOT}/libcef_dll/wrapper/cef_scoped_temp_dir.cc
    ${CEF_ROOT}/libcef_dll/wrapper/cef_stream_resource_handler.cc
    ${CEF_ROOT}/libcef_dll/wrapper/cef_xml_object.cc
    ${CEF_ROOT}/libcef_dll/wrapper/cef_zip_archive.cc
    ${CEF_ROOT}/libcef_dll/wrapper/libcef_dll_wrapper.cc
    ${CEF_ROOT}/libcef_dll/wrapper/libcef_dll_wrapper2.cc
)

if(APPLE)
    list(APPEND CEF_WRAPPER_SOURCES
        ${CEF_ROOT}/libcef_dll/wrapper/cef_library_loader_mac.mm)
endif()

file(GLOB CEF_BASE_SOURCES "${CEF_ROOT}/libcef_dll/base/*.cc")
file(GLOB CEF_CPPTOC_SOURCES "${CEF_ROOT}/libcef_dll/cpptoc/*.cc")
file(GLOB CEF_CTOCPP_SOURCES "${CEF_ROOT}/libcef_dll/ctocpp/*.cc")
file(GLOB CEF_TRANSFER_SOURCES
    "${CEF_ROOT}/libcef_dll/transfer_util.cc"
    "${CEF_ROOT}/libcef_dll/shutdown_checker.cc")

add_library(cef_dll_wrapper STATIC
    ${CEF_WRAPPER_SOURCES}
    ${CEF_BASE_SOURCES}
    ${CEF_CPPTOC_SOURCES}
    ${CEF_CTOCPP_SOURCES}
    ${CEF_TRANSFER_SOURCES}
)

target_include_directories(cef_dll_wrapper PUBLIC ${CEF_ROOT} ${CEF_ROOT}/include)
target_compile_definitions(cef_dll_wrapper PRIVATE WRAPPING_CEF_SHARED)

if(APPLE)
    target_compile_options(cef_dll_wrapper PRIVATE -Wno-undefined-var-template)
elseif(WIN32)
    target_compile_definitions(cef_dll_wrapper PRIVATE
        WIN32 _WIN32 _WINDOWS UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# =============================================================================
# CEF imported library
# =============================================================================

if(APPLE)
    set(CEF_FRAMEWORK_DIR "${CEF_ROOT}/Release/Chromium Embedded Framework.framework")
    add_library(cef SHARED IMPORTED GLOBAL)
    set_target_properties(cef PROPERTIES
        IMPORTED_LOCATION "${CEF_FRAMEWORK_DIR}/Chromium Embedded Framework"
        INTERFACE_INCLUDE_DIRECTORIES "${CEF_ROOT};${CEF_ROOT}/include")
elseif(WIN32)
    add_library(cef SHARED IMPORTED GLOBAL)
    set_target_properties(cef PROPERTIES
        IMPORTED_LOCATION "${CEF_ROOT}/Release/libcef.dll"
        IMPORTED_IMPLIB "${CEF_ROOT}/Release/libcef.lib"
        INTERFACE_INCLUDE_DIRECTORIES "${CEF_ROOT};${CEF_ROOT}/include")
else()
    add_library(cef SHARED IMPORTED GLOBAL)
    set_target_properties(cef PROPERTIES
        IMPORTED_LOCATION "${CEF_ROOT}/Release/libcef.so"
        INTERFACE_INCLUDE_DIRECTORIES "${CEF_ROOT};${CEF_ROOT}/include")
endif()

# =============================================================================
# WebGPU includes from vivid build tree
# =============================================================================

# Find wgpu include path in vivid's build directory
file(GLOB WGPU_CANDIDATES "${VIVID_BUILD_DIR}/_deps/wgpu*-src/include")
set(WGPU_INCLUDE_DIR "")
foreach(candidate ${WGPU_CANDIDATES})
    if(EXISTS "${candidate}/webgpu/webgpu.h")
        set(WGPU_INCLUDE_DIR "${candidate}")
        break()
    endif()
endforeach()

if(WGPU_INCLUDE_DIR)
    message(STATUS "[vivid-cef] WebGPU includes: ${WGPU_INCLUDE_DIR}")
else()
    message(WARNING "[vivid-cef] Could not find WebGPU includes in ${VIVID_BUILD_DIR}/_deps/")
endif()

# =============================================================================
# browser operator plugin (MODULE library)
# =============================================================================

add_library(browser MODULE
    src/browser_op.cpp
    src/cef_manager.cpp
    src/cef_client.cpp
)

target_include_directories(browser PRIVATE
    ${VIVID_SRC_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${WGPU_INCLUDE_DIR}
)

target_link_libraries(browser PRIVATE cef_dll_wrapper cef)

set_target_properties(browser PROPERTIES
    PREFIX ""
    SUFFIX "${VIVID_PLUGIN_SUFFIX}"
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

if(APPLE)
    target_link_libraries(browser PRIVATE
        "-framework CoreFoundation"
        "-framework CoreGraphics"
        "-framework AppKit"
    )
    target_link_options(browser PRIVATE -undefined dynamic_lookup)
    set_target_properties(browser PROPERTIES
        INSTALL_RPATH "@loader_path"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
elseif(WIN32)
    target_link_libraries(browser PRIVATE d3d11 dxgi)
endif()

# =============================================================================
# CEF subprocess helper
# =============================================================================

add_executable(vivid-cef-helper subprocess/main.cpp)
target_link_libraries(vivid-cef-helper PRIVATE cef_dll_wrapper cef)
target_include_directories(vivid-cef-helper PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

set_target_properties(vivid-cef-helper PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

# =============================================================================
# Deploy CEF framework/DLLs alongside plugin
# =============================================================================

if(APPLE)
    add_custom_command(TARGET browser POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CEF_FRAMEWORK_DIR}"
            "${CMAKE_BINARY_DIR}/Chromium Embedded Framework.framework"
        # Fix CEF framework reference: plugins are loaded via dlopen so
        # @executable_path resolves to the host app, not the plugin dir.
        # Use @loader_path so the framework is found next to the dylib.
        COMMAND install_name_tool -change
            "@executable_path/../Frameworks/Chromium Embedded Framework.framework/Chromium Embedded Framework"
            "@loader_path/Chromium Embedded Framework.framework/Chromium Embedded Framework"
            "${CMAKE_BINARY_DIR}/browser${VIVID_PLUGIN_SUFFIX}"
        COMMENT "[vivid-cef] Copying CEF framework and fixing plugin rpath"
    )
    add_custom_command(TARGET vivid-cef-helper POST_BUILD
        COMMAND install_name_tool -change
            "@executable_path/../Frameworks/Chromium Embedded Framework.framework/Chromium Embedded Framework"
            "@executable_path/Chromium Embedded Framework.framework/Chromium Embedded Framework"
            "${CMAKE_BINARY_DIR}/vivid-cef-helper"
        COMMENT "[vivid-cef] Fixing helper rpath"
    )
elseif(WIN32)
    add_custom_command(TARGET browser POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CEF_ROOT}/Release" "${CMAKE_BINARY_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CEF_ROOT}/Resources" "${CMAKE_BINARY_DIR}"
        COMMENT "[vivid-cef] Copying CEF binaries to build directory"
    )
else()
    add_custom_command(TARGET browser POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CEF_ROOT}/Release" "${CMAKE_BINARY_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CEF_ROOT}/Resources" "${CMAKE_BINARY_DIR}"
        COMMENT "[vivid-cef] Copying CEF binaries to build directory"
    )
endif()

message(STATUS "[vivid-cef] CEF ${CEF_VERSION} configured for ${CEF_PLATFORM}")
