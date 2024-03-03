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

include(FetchContent)
FetchContent_Declare(
    swal
    GIT_REPOSITORY https://github.com/disba1ancer/swal.git
    GIT_TAG master
    FIND_PACKAGE_ARGS
)

FetchContent_MakeAvailable(swal)
add_executable(test
    src/TestPC.cpp
    src/TestPC.h
    src/main.cpp
)
target_compile_definitions(test PRIVATE UNICODE=1 _WIN32_WINNT=0x0A00)
target_link_libraries(test PRIVATE cpu86e swal::swal)

install(TARGETS cpu86e
    EXPORT cpu86e
    FILE_SET HEADERS
)
