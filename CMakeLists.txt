cmake_minimum_required(VERSION 3.10)
project(Chan C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Build the interpreter for minimum binary size (MSVC: /O1 or /Os).
option(CHAN_SIZE_OPT "Optimize for binary size" OFF)
if(CHAN_SIZE_OPT)
    set(CMAKE_C_FLAGS_RELEASE "/O1 /MD /DNDEBUG")
endif()

add_executable(chan src/main.c src/lexer.c src/parser.c src/value.c src/interp.c)

target_include_directories(chan PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Minimal embedding: the same core library without the demo host (main.c).
# Useful to measure the footprint of the language itself.
add_executable(chan_core tests/embed_min.c src/lexer.c src/parser.c src/value.c src/interp.c)
target_include_directories(chan_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
