cmake_minimum_required(VERSION 3.21)

# ---------------------------------------------------------------------------
# Version: single source of truth is VERSION.txt at the repository root.
# CMake, the installer script and the CI workflows all read that one file.
# ---------------------------------------------------------------------------
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION.txt" SURL_VERSION_RAW)
string(STRIP "${SURL_VERSION_RAW}" SURL_VERSION)

project(surl
        VERSION ${SURL_VERSION}
        DESCRIPTION "Fast native C++ CLI tool for creating local copies of publicly accessible web resources"
        HOMEPAGE_URL "https://github.com/sorrysvj/surl"
        LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(SURL_BUILD_TESTS "Build the SURL test suite" ON)
option(SURL_WERROR      "Treat compiler warnings as errors" OFF)
option(SURL_STATIC_RUNTIME
       "Link the C++ runtime statically so surl.exe ships without redistributables"
       ${WIN32})

set(SURL_GITHUB_OWNER "sorrysvj" CACHE STRING "GitHub owner used by 'surl update'")
set(SURL_GITHUB_REPO  "surl"     CACHE STRING "GitHub repository used by 'surl update'")

include(GNUInstallDirs)

# ---------------------------------------------------------------------------
# Generated version header
# ---------------------------------------------------------------------------
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/surl/version.hpp"
    @ONLY)

# ---------------------------------------------------------------------------
# Core library (everything except main()) so tests can link against it
# ---------------------------------------------------------------------------
set(SURL_CORE_SOURCES
    src/util/log.cpp
    src/util/hash.cpp
    src/util/strings.cpp
    src/util/fsutil.cpp
    src/util/json.cpp
    src/util/mime.cpp
    src/util/paths.cpp
    src/net/url.cpp
    src/net/http_client.cpp
    src/net/robots.cpp
    src/parse/html.cpp
    src/parse/css.cpp
    src/mirror/pathmap.cpp
    src/mirror/cache.cpp
    src/mirror/rewrite.cpp
    src/mirror/crawler.cpp
    src/cli/options.cpp
    src/cli/help.cpp
    src/cmd/commands.cpp
    src/cmd/serve.cpp
    src/cmd/inspect.cpp
    src/cmd/maintenance.cpp
    src/cmd/update.cpp
)

if(WIN32)
    list(APPEND SURL_CORE_SOURCES src/net/http_winhttp.cpp)
else()
    list(APPEND SURL_CORE_SOURCES src/net/http_curl.cpp)
endif()

add_library(surl_core STATIC ${SURL_CORE_SOURCES})

target_include_directories(surl_core
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_BINARY_DIR}/generated")

target_compile_features(surl_core PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(surl_core PUBLIC
        /utf-8 /W4 /permissive- /EHsc /Zc:__cplusplus /Zc:preprocessor /MP)
    target_compile_definitions(surl_core PUBLIC
        UNICODE _UNICODE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        _CRT_SECURE_NO_WARNINGS)
    if(SURL_WERROR)
        target_compile_options(surl_core PUBLIC /WX)
    endif()
else()
    target_compile_options(surl_core PUBLIC -Wall -Wextra -Wpedantic)
    if(SURL_WERROR)
        target_compile_options(surl_core PUBLIC -Werror)
    endif()
endif()

if(WIN32)
    target_link_libraries(surl_core PUBLIC winhttp ws2_32 shlwapi advapi32)
    if(MINGW)
        # MinGW needs these spelled out for std::thread / filesystem.
        target_link_libraries(surl_core PUBLIC bcrypt)
    endif()
else()
    find_package(CURL REQUIRED)
    find_package(Threads REQUIRED)
    target_link_libraries(surl_core PUBLIC CURL::libcurl Threads::Threads)
endif()

if(SURL_STATIC_RUNTIME)
    if(MSVC)
        set_property(TARGET surl_core PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    elseif(MINGW)
        target_link_options(surl_core PUBLIC
            -static -static-libgcc -static-libstdc++)
    endif()
endif()

# ---------------------------------------------------------------------------
# The surl executable
# ---------------------------------------------------------------------------
set(SURL_EXE_SOURCES src/main.cpp)

if(WIN32)
    # rc.exe reads .rc files as ANSI, so an absolute path embedded in one is
    # mangled whenever the source tree lives under a non-ASCII directory. Copy
    # the icon next to the generated .rc and reference it by bare name, which
    # keeps the .rc pure ASCII and the build working for every user name.
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/installer/windows/assets/surl.ico"
        "${CMAKE_CURRENT_BINARY_DIR}/generated/surl.ico"
        COPYONLY)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/surl.rc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/generated/surl.rc"
        @ONLY)
    list(APPEND SURL_EXE_SOURCES "${CMAKE_CURRENT_BINARY_DIR}/generated/surl.rc")
    # rc.exe resolves a bare resource name against its own include path.
    set_source_files_properties(
        "${CMAKE_CURRENT_BINARY_DIR}/generated/surl.rc"
        PROPERTIES COMPILE_OPTIONS "/I${CMAKE_CURRENT_BINARY_DIR}/generated")
endif()

add_executable(surl ${SURL_EXE_SOURCES})
target_link_libraries(surl PRIVATE surl_core)
set_target_properties(surl PROPERTIES OUTPUT_NAME "surl")

if(SURL_STATIC_RUNTIME AND MSVC)
    set_property(TARGET surl PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(MSVC)
    # Embed the manifest that declares Windows 10/11 awareness and long-path support.
    target_link_options(surl PRIVATE
        "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/src/platform/surl.manifest"
        "/MANIFEST:EMBED")
endif()

install(TARGETS surl RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES LICENSE README.md DESTINATION ${CMAKE_INSTALL_DOCDIR})

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(SURL_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
message(STATUS "----------------------------------------------------------")
message(STATUS "SURL ${SURL_VERSION}")
message(STATUS "  compiler        : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  generator       : ${CMAKE_GENERATOR}")
message(STATUS "  build type      : ${CMAKE_BUILD_TYPE}")
message(STATUS "  static runtime  : ${SURL_STATIC_RUNTIME}")
message(STATUS "  tests           : ${SURL_BUILD_TESTS}")
message(STATUS "  update source   : ${SURL_GITHUB_OWNER}/${SURL_GITHUB_REPO}")
message(STATUS "----------------------------------------------------------")
