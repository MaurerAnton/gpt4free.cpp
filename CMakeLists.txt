cmake_minimum_required(VERSION 3.16)
project(gpt4free_cpp VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(CURL REQUIRED)

# nlohmann/json: system first, FetchContent fallback
find_package(nlohmann_json 3 QUIET)
if(NOT nlohmann_json_FOUND)
  include(FetchContent)
  FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz)
  FetchContent_MakeAvailable(nlohmann_json)
endif()

add_library(g4f
  src/models.cpp
  src/http.cpp
  src/providers/deepseek.cpp
)
target_include_directories(g4f PUBLIC include)
target_link_libraries(g4f PUBLIC CURL::libcurl nlohmann_json::nlohmann_json)
target_compile_options(g4f PRIVATE -Wall -Wextra)

add_executable(g4f_tests
  tests/main.cpp
)
target_link_libraries(g4f_tests PRIVATE g4f)

enable_testing()
add_test(NAME g4f_tests COMMAND g4f_tests)
