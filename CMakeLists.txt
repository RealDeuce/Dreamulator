cmake_minimum_required(VERSION 3.14)
project(dreamulator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

include(CheckCXXCompilerFlag)
include(CheckCXXSourceCompiles)

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

find_package(FLTK REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(PORTAUDIO portaudio-2.0)

add_executable(dreamulator
    src/main.cpp
    src/v20.cpp
    src/machine.cpp
    src/uart.cpp
    src/fdc.cpp
    src/dis86.cpp
    src/prefs.cpp
    src/dbg_regs.cpp
    src/dbg_dis.cpp
    src/dbg_mem.cpp
    src/dbg_periph.cpp
    src/remote.cpp
)

target_include_directories(dreamulator SYSTEM PRIVATE
    ${FLTK_INCLUDE_DIRS}
)

target_link_libraries(dreamulator PRIVATE
    ${FLTK_LIBRARIES}
)

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
target_compile_options(dreamulator PRIVATE -Wall -Wextra)
try_add_compile_option(dreamulator -Wformat=2)
try_add_compile_option(dreamulator -Wconversion)
try_add_compile_option(dreamulator -Wsign-conversion)
try_add_compile_option(dreamulator -Wtrampolines)
try_add_compile_option(dreamulator -Wimplicit-fallthrough)
try_add_compile_option(dreamulator -Wbidi-chars=any)
try_add_compile_option(dreamulator -Werror=format-security)

# -Werror only for Debug builds
target_compile_options(dreamulator PRIVATE $<$<CONFIG:Debug>:-Werror>)

# Runtime hardening
target_compile_definitions(dreamulator PRIVATE _GLIBCXX_ASSERTIONS)

set(CMAKE_REQUIRED_FLAGS "-O2 -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=3")
check_cxx_source_compiles("#include <string.h>\nint main() { return 0; }" HAS_FORTIFY_SOURCE_3)
unset(CMAKE_REQUIRED_FLAGS)
if(HAS_FORTIFY_SOURCE_3)
    target_compile_options(dreamulator PRIVATE
        $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>,$<CONFIG:MinSizeRel>>:-U_FORTIFY_SOURCE>
        $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>,$<CONFIG:MinSizeRel>>:-D_FORTIFY_SOURCE=3>)
endif()

try_add_compile_option(dreamulator -fstrict-flex-arrays=3)
try_add_compile_option(dreamulator -fstack-clash-protection)
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
try_add_compile_option(dreamulator -fPIE)

# Linker hardening
try_add_link_option(dreamulator -pie)
try_add_link_option(dreamulator "-Wl,-z,noexecstack")
try_add_link_option(dreamulator "-Wl,-z,relro")
try_add_link_option(dreamulator "-Wl,-z,now")
try_add_link_option(dreamulator "-Wl,--as-needed")
try_add_link_option(dreamulator "-Wl,--no-copy-dt-needed-entries")

install(TARGETS dreamulator RUNTIME DESTINATION bin)
install(DIRECTORY roms/ DESTINATION share/dreamulator/roms
        FILES_MATCHING PATTERN "*.ic303" PATTERN "*.bin")
