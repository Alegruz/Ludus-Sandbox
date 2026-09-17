cmake_minimum_required(VERSION 3.25)

project(
    LudusSandbox
    VERSION 0.1.0
    LANGUAGES CXX
)

find_package(Ludus CONFIG REQUIRED)

add_executable(LudusSandbox
    src/main.cpp
)

target_compile_features(
    LudusSandbox
    PRIVATE
        cxx_std_23
)

target_link_libraries(
    LudusSandbox
    PRIVATE
        Ludus::FoundationBase
)