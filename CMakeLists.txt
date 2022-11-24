cmake_minimum_required(VERSION 3.17)
project(go_symbol)

set(CMAKE_CXX_STANDARD 17)

set(CMAKE_POSITION_INDEPENDENT_CODE TRUE)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/lib)

include(FetchContent)

FetchContent_Declare(
        zero
        GIT_REPOSITORY https://github.com/Hackerl/zero
        GIT_TAG beb65ec9f125b77a5163cb7ee80f037fb2f58ca4
)

FetchContent_MakeAvailable(zero)

FetchContent_Declare(
        elf
        GIT_REPOSITORY https://github.com/Hackerl/elf
        GIT_TAG f785d01d27d1d72882be23862f5bdda363903aeb
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