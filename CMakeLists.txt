cmake_minimum_required(VERSION 3.5)

project(x86emu LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(x86emu STATIC
    src/cpu.cpp
)
target_sources(x86emu PUBLIC FILE_SET HEADERS BASE_DIRS src/include FILES
    src/include/x86emu/cpu.h
    src/include/x86emu/iiohook.h
)

add_executable(test src/main.cpp)
target_link_libraries(test PRIVATE x86emu)

install(TARGETS x86emu
    EXPORT x86emu
    FILE_SET HEADERS
)
