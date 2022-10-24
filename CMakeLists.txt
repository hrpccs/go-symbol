cmake_minimum_required(VERSION 3.17)
project(go_symbol)

set(CMAKE_CXX_STANDARD 17)

set(CMAKE_POSITION_INDEPENDENT_CODE TRUE)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/lib)

include(FetchContent)

FetchContent_Declare(
        zero
        GIT_REPOSITORY https://github.com/Hackerl/zero
        GIT_TAG 8f6fafd8d148f13a6cbef1322037f0e8ea1ffb8b
)

FetchContent_MakeAvailable(zero)

FetchContent_Declare(
        elf
        GIT_REPOSITORY https://github.com/Hackerl/elf
        GIT_TAG 7264908e9fc7f8ab991071f851e5c34bc22cfcf6
)

FetchContent_MakeAvailable(elf)

add_library(
        go_symbol
        src/binary/binary.cpp
        src/symbol/reader.cpp
        src/symbol/symbol.cpp
        src/symbol/build_info.cpp
)

target_include_directories(go_symbol PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(go_symbol zero elf)