cmake_minimum_required(VERSION 3.5)

project(cpu86e LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(cpu86e STATIC
    src/cpu.cpp
)
target_sources(cpu86e PUBLIC FILE_SET HEADERS BASE_DIRS src/include FILES
    src/include/cpu86e/cpu.h
    src/include/cpu86e/iiohook.h
)

add_executable(test src/main.cpp)
target_link_libraries(test PRIVATE cpu86e)

install(TARGETS cpu86e
    EXPORT cpu86e
    FILE_SET HEADERS
)
