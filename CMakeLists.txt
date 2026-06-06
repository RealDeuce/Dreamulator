cmake_minimum_required(VERSION 3.14)
project(dreamulator VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)

include(CheckCXXCompilerFlag)
include(CheckCXXSourceCompiles)
include(FetchContent)

function(try_add_compile_option target flag)
    string(REGEX REPLACE "[^a-zA-Z0-9]" "_" var "HAS_CXX${flag}")
    check_cxx_compiler_flag("${flag}" ${var})
    if(${var})
        target_compile_options(${target} PRIVATE "${flag}")
    endif()
endfunction()

function(try_add_link_option target flag)
    string(REGEX REPLACE "[^a-zA-Z0-9]" "_" var "HAS_LD${flag}")
    set(CMAKE_REQUIRED_LINK_OPTIONS "${flag}")
    check_cxx_source_compiles("int main() { return 0; }" ${var})
    if(${var})
        target_link_options(${target} PRIVATE "${flag}")
    endif()
endfunction()

# --- Dependencies: find_package first, FetchContent fallback ---

find_package(ZLIB QUIET)
if(NOT ZLIB_FOUND)
    message(STATUS "ZLIB not found — using FetchContent")
    FetchContent_Declare(zlib
        URL https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz
        URL_HASH SHA256=9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23)
    set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(zlib)
    add_library(ZLIB::ZLIB ALIAS zlibstatic)
endif()

find_package(FLTK QUIET CONFIG)
if(TARGET fltk::fltk)
    message(STATUS "FLTK found (target fltk::fltk)")
    set(FLTK_TARGET fltk::fltk)
endif()
if(NOT FLTK_TARGET)
    unset(FLTK_FOUND CACHE)
    find_package(FLTK QUIET)
    if(FLTK_FOUND AND FLTK_LIBRARIES)
        message(STATUS "FLTK found (module): libs=${FLTK_LIBRARIES}")
        set(FLTK_TARGET ${FLTK_LIBRARIES})
        if(NOT FLTK_INCLUDE_DIRS)
            find_path(FLTK_INCLUDE_DIRS FL/Fl.H)
        endif()
    endif()
endif()
if(NOT FLTK_TARGET)
    message(STATUS "FLTK not found — using FetchContent")
    set(FLTK_BUILD_TEST OFF CACHE BOOL "" FORCE)
    set(FLTK_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(FLTK_BUILD_FLUID OFF CACHE BOOL "" FORCE)
    set(FLTK_BUILD_GL OFF CACHE BOOL "" FORCE)
    set(OPTION_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(fltk
        URL https://github.com/fltk/fltk/releases/download/release-1.4.3/fltk-1.4.3-source.tar.gz
        URL_HASH SHA256=19725f687146610976a1a43ef448feeba212a7eca88ad5897d22f6adaf3af45b)
    FetchContent_MakeAvailable(fltk)
    set(FLTK_TARGET fltk::fltk)
endif()

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND AND NOT CMAKE_CROSSCOMPILING)
    pkg_check_modules(PORTAUDIO portaudio-2.0)
endif()
if(NOT PORTAUDIO_FOUND)
    FetchContent_Declare(portaudio
        URL https://github.com/PortAudio/portaudio/archive/refs/tags/v19.7.0.tar.gz
        URL_HASH SHA256=5af29ba58bbdbb7bbcefaaecc77ec8fc413f0db6f4c4e286c40c3e1b83174fa0)
    set(PA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(PA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(portaudio)
    set(PORTAUDIO_FOUND TRUE)
    set(PORTAUDIO_LIBRARIES portaudio_static)
    set(PORTAUDIO_INCLUDE_DIRS ${portaudio_SOURCE_DIR}/include)
endif()

set(PRINT_SOURCES
    src/print/printer.cpp
    src/print/escp.cpp
    src/print/pcl.cpp
    src/print/imagewriter.cpp
    src/print/dotrender.cpp
    src/print/font9pin.cpp
    src/print/fontfx80.cpp
    src/print/fontiw2.cpp
    src/print/font24pin.cpp
    src/print/pagebuf.cpp
    src/print/pdfwriter.cpp
)

set(EMU_SOURCES
    src/main.cpp
    src/v20.cpp
    src/machine.cpp
    src/paths.cpp
    src/uart.cpp
    src/fdc.cpp
    src/dis86.cpp
    src/prefs.cpp
    src/dbg_regs.cpp
    src/dbg_dis.cpp
    src/dbg_mem.cpp
    src/dbg_periph.cpp
    src/remote.cpp
    ${PRINT_SOURCES}
)

if(WIN32)
    list(APPEND EMU_SOURCES src/compat/compat_win32.cpp)
endif()

add_executable(dreamulator ${EMU_SOURCES})

if(FLTK_INCLUDE_DIRS)
    target_include_directories(dreamulator SYSTEM PRIVATE ${FLTK_INCLUDE_DIRS})
endif()

if(WIN32)
    target_include_directories(dreamulator BEFORE PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/compat)
endif()

target_link_libraries(dreamulator PRIVATE
    ${FLTK_TARGET}
    ZLIB::ZLIB
)

if(WIN32)
    target_compile_definitions(dreamulator PRIVATE WIN32_LEAN_AND_MEAN)
    target_link_libraries(dreamulator PRIVATE ws2_32 ole32 uuid comctl32 comdlg32)
endif()
if(MSVC)
    target_compile_definitions(dreamulator PRIVATE
        strcasecmp=_stricmp strncasecmp=_strnicmp)
endif()

if(PORTAUDIO_FOUND)
    target_compile_definitions(dreamulator PRIVATE HAS_PORTAUDIO)
    target_include_directories(dreamulator SYSTEM PRIVATE ${PORTAUDIO_INCLUDE_DIRS})
    target_link_directories(dreamulator PRIVATE ${PORTAUDIO_LIBRARY_DIRS})
    target_link_libraries(dreamulator PRIVATE ${PORTAUDIO_LIBRARIES})
    message(STATUS "PortAudio found - audio enabled")
else()
    message(STATUS "PortAudio not found - building without audio")
endif()

# --- Compiler Hardening (OpenSSF Guide) ---

# Warnings
if(MSVC)
    target_compile_options(dreamulator PRIVATE /W4)
    target_compile_options(dreamulator PRIVATE $<$<CONFIG:Debug>:/WX>)
else()
    target_compile_options(dreamulator PRIVATE -Wall -Wextra)
    try_add_compile_option(dreamulator -Wformat=2)
    try_add_compile_option(dreamulator -Wconversion)
    try_add_compile_option(dreamulator -Wsign-conversion)
    try_add_compile_option(dreamulator -Wtrampolines)
    try_add_compile_option(dreamulator -Wimplicit-fallthrough)
    try_add_compile_option(dreamulator -Wbidi-chars=any)
    try_add_compile_option(dreamulator -Werror=format-security)
    target_compile_options(dreamulator PRIVATE $<$<CONFIG:Debug>:-Werror>)
endif()

# Runtime hardening
if(NOT MSVC)
    target_compile_definitions(dreamulator PRIVATE _GLIBCXX_ASSERTIONS)
endif()

if(NOT MSVC)
    set(CMAKE_REQUIRED_FLAGS "-O2 -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=3")
    check_cxx_source_compiles("#include <string.h>\nint main() { return 0; }" HAS_FORTIFY_SOURCE_3)
    unset(CMAKE_REQUIRED_FLAGS)
    if(HAS_FORTIFY_SOURCE_3)
        target_compile_options(dreamulator PRIVATE
            $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>,$<CONFIG:MinSizeRel>>:-U_FORTIFY_SOURCE>
            $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>,$<CONFIG:MinSizeRel>>:-D_FORTIFY_SOURCE=3>)
    endif()

    try_add_compile_option(dreamulator -fstrict-flex-arrays=3)
    try_add_compile_option(dreamulator -fstack-protector-strong)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "[xX]86_64|[aA][mM][dD]64|i[3-6]86")
        try_add_compile_option(dreamulator -fcf-protection=full)
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "[aA][aA]rch64|ARM64")
        try_add_compile_option(dreamulator -mbranch-protection=standard)
    endif()
    try_add_compile_option(dreamulator -fno-delete-null-pointer-checks)
    try_add_compile_option(dreamulator -fno-strict-overflow)
    try_add_compile_option(dreamulator -fno-strict-aliasing)
    try_add_compile_option(dreamulator -ftrivial-auto-var-init=zero)
    try_add_compile_option(dreamulator -fzero-init-padding-bits=all)
endif()

if(NOT WIN32)
    try_add_compile_option(dreamulator -fstack-clash-protection)
    try_add_compile_option(dreamulator -fPIE)

    # ELF-specific linker hardening
    try_add_link_option(dreamulator -pie)
    try_add_link_option(dreamulator "-Wl,-z,noexecstack")
    try_add_link_option(dreamulator "-Wl,-z,relro")
    try_add_link_option(dreamulator "-Wl,-z,now")
    try_add_link_option(dreamulator "-Wl,--as-needed")
    try_add_link_option(dreamulator "-Wl,--no-copy-dt-needed-entries")
endif()

# --- dreamprint: printer simulator tool ---

set(DREAMPRINT_SOURCES src/print/dreamprint.cpp ${PRINT_SOURCES})
if(WIN32)
    list(APPEND DREAMPRINT_SOURCES src/compat/compat_win32.cpp)
endif()

add_executable(dreamprint ${DREAMPRINT_SOURCES})

target_link_libraries(dreamprint PRIVATE ZLIB::ZLIB)

if(WIN32)
    target_compile_definitions(dreamprint PRIVATE WIN32_LEAN_AND_MEAN)
    target_include_directories(dreamprint BEFORE PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/compat)
    target_link_libraries(dreamprint PRIVATE ws2_32)
endif()
if(MSVC)
    target_compile_definitions(dreamprint PRIVATE
        strcasecmp=_stricmp strncasecmp=_strnicmp)
endif()

# Apply same hardening flags to dreamprint
if(MSVC)
    target_compile_options(dreamprint PRIVATE /W4)
    target_compile_options(dreamprint PRIVATE $<$<CONFIG:Debug>:/WX>)
else()
    target_compile_options(dreamprint PRIVATE -Wall -Wextra)
    try_add_compile_option(dreamprint -Wformat=2)
    try_add_compile_option(dreamprint -Wconversion)
    try_add_compile_option(dreamprint -Wsign-conversion)
    try_add_compile_option(dreamprint -Wimplicit-fallthrough)
    try_add_compile_option(dreamprint -Werror=format-security)
    target_compile_options(dreamprint PRIVATE $<$<CONFIG:Debug>:-Werror>)
endif()

install(TARGETS dreamulator dreamprint RUNTIME DESTINATION bin)
install(DIRECTORY roms/ DESTINATION share/dreamulator/roms
        FILES_MATCHING PATTERN "*.ic303" PATTERN "*.bin")

# --- CPack ---

set(CPACK_PACKAGE_NAME "dreamulator")
set(CPACK_PACKAGE_VENDOR "Stephen Hurd")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Nakajima/DreamWriter typewriter emulator")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/RealDeuce/Dreamulator")
set(CPACK_PACKAGE_CONTACT "claude@sjh.sh")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")

if(APPLE)
    set(CPACK_GENERATOR DragNDrop)
    set(CPACK_DMG_VOLUME_NAME "Dreamulator")
elseif(WIN32)
    set(CPACK_GENERATOR NSIS)
    set(CPACK_NSIS_DISPLAY_NAME "Dreamulator")
    set(CPACK_NSIS_INSTALL_ROOT "$PROGRAMFILES64")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_NSIS_CREATE_ICONS_EXTRA
        "CreateShortCut '$SMPROGRAMS\\\\$STARTMENU_FOLDER\\\\Dreamulator.lnk' '$INSTDIR\\\\bin\\\\dreamulator.exe'")
    set(CPACK_NSIS_DELETE_ICONS_EXTRA
        "Delete '$SMPROGRAMS\\\\$START_MENU\\\\Dreamulator.lnk'")
else()
    set(CPACK_GENERATOR "DEB;RPM;TGZ")

    # DEB
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libfltk1.3 (>= 1.3), zlib1g, libportaudio2")
    set(CPACK_DEBIAN_PACKAGE_SECTION "misc")

    # RPM
    set(CPACK_RPM_PACKAGE_REQUIRES "fltk, zlib, portaudio")
    set(CPACK_RPM_PACKAGE_LICENSE "BSD-3-Clause")
endif()

include(CPack)
