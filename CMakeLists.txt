cmake_minimum_required(VERSION 3.16)
project(scriptwatch LANGUAGES CXX OBJCXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64")

add_library(scriptwatch SHARED
    src/plugin.cpp
    src/utils.mm
    src/utils.hpp
)

target_include_directories(scriptwatch PRIVATE third-party/ida-sdk/src/include)

set_target_properties(scriptwatch PROPERTIES
    PREFIX ""
    LINK_FLAGS "-undefined dynamic_lookup"
)

target_link_libraries(scriptwatch PRIVATE "-framework Cocoa")
target_compile_options(scriptwatch PRIVATE -Wno-nullability-completeness)

add_custom_command(TARGET scriptwatch POST_BUILD
    COMMAND /bin/bash "${CMAKE_CURRENT_SOURCE_DIR}/copy_to_ida.sh" "${CMAKE_CURRENT_BINARY_DIR}"
    VERBATIM
)
