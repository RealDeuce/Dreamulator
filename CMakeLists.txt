cmake_minimum_required(VERSION 3.14)
project(dreamulator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)

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

target_compile_options(dreamulator PRIVATE -Wall -Wextra)

install(TARGETS dreamulator RUNTIME DESTINATION bin)
install(DIRECTORY roms/ DESTINATION share/dreamulator/roms
        FILES_MATCHING PATTERN "*.ic303" PATTERN "*.bin")
