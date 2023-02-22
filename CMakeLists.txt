cmake_minimum_required(VERSION 3.17)
project(go_symbol)

set(CMAKE_CXX_STANDARD 17)

set(CMAKE_POSITION_INDEPENDENT_CODE TRUE)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/lib)

include(FetchContent)

FetchContent_Declare(
        zero
        GIT_REPOSITORY https://github.com/Hackerl/zero
        GIT_TAG df739d44cacc95cf10251c0dbede56bc1f0b18bb
)

FetchContent_MakeAvailable(zero)

FetchContent_Declare(
        elf
        GIT_REPOSITORY https://github.com/Hackerl/elf
        GIT_TAG cbf7f5ffb5a3c07f6a8f02d5fc9e425dca572bbc
)

FetchContent_MakeAvailable(elf)

add_library(
        go_symbol
        src/binary.cpp
        src/version.cpp
        src/symbol/reader.cpp
        src/symbol/symbol.cpp
        src/symbol/build_info.cpp
        src/symbol/interface.cpp
)

target_include_directories(go_symbol PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(go_symbol zero elf_cpp)