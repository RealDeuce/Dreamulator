cmake_minimum_required(VERSION 3.14)
project(dreamulator LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 11)

find_package(FLTK REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(PORTAUDIO REQUIRED portaudio-2.0)

add_executable(dreamulator
    src/main.cpp
    src/v20.c
    src/machine.c
    src/uart.c
    src/fdc.c
)

target_include_directories(dreamulator SYSTEM PRIVATE
    ${FLTK_INCLUDE_DIRS}
    ${PORTAUDIO_INCLUDE_DIRS}
)

target_link_directories(dreamulator PRIVATE
    ${PORTAUDIO_LIBRARY_DIRS}
)

target_link_libraries(dreamulator PRIVATE
    ${FLTK_LIBRARIES}
    ${PORTAUDIO_LIBRARIES}
)

target_compile_options(dreamulator PRIVATE
    $<$<COMPILE_LANGUAGE:C>:-Wall -Wextra>
    $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra>
)

install(TARGETS dreamulator RUNTIME DESTINATION bin)
install(DIRECTORY roms/ DESTINATION share/dreamulator/roms
        FILES_MATCHING PATTERN "*.ic303" PATTERN "*.bin")
