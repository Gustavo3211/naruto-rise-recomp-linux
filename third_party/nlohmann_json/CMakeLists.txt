cmake_minimum_required(VERSION 3.25)
project(nlohmann_json LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

add_library(narutorise::json ALIAS nlohmann_json)
