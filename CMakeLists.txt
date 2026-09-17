cmake_minimum_required(VERSION 3.16)
project(gpt4free_cpp VERSION 0.1.0 LANGUAGES C CXX)

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
  src/providers/envelope.cpp
  src/providers/pow.cpp
  src/providers/pow_wasm3.cpp
  src/providers/sse.cpp
)
target_include_directories(g4f PUBLIC include)
target_link_libraries(g4f PUBLIC CURL::libcurl nlohmann_json::nlohmann_json)
target_compile_options(g4f PRIVATE -Wall -Wextra)

# wasm3 interpreter (native PoW runtime) — same role as wasmtime upstream.
cmake_policy(SET CMP0169 OLD) # allow direct FetchContent_Populate (no name clash: we build selected sources only)
FetchContent_Declare(wasm3
  GIT_REPOSITORY https://github.com/wasm3/wasm3.git
  GIT_TAG v0.9.1-beta.1
  GIT_SHALLOW TRUE
)
FetchContent_GetProperties(wasm3)
if(NOT wasm3_POPULATED)
  FetchContent_Populate(wasm3)
endif()
file(GLOB WASM3_SOURCES CONFIGURE_DEPENDS
  ${wasm3_SOURCE_DIR}/source/m3_api_libc.c
  ${wasm3_SOURCE_DIR}/source/m3_api_wasi.c
  ${wasm3_SOURCE_DIR}/source/m3_bind.c
  ${wasm3_SOURCE_DIR}/source/m3_code.c
  ${wasm3_SOURCE_DIR}/source/m3_compile.c
  ${wasm3_SOURCE_DIR}/source/m3_core.c
  ${wasm3_SOURCE_DIR}/source/m3_env.c
  ${wasm3_SOURCE_DIR}/source/m3_exec.c
  ${wasm3_SOURCE_DIR}/source/m3_function.c
  ${wasm3_SOURCE_DIR}/source/m3_info.c
  ${wasm3_SOURCE_DIR}/source/m3_module.c
  ${wasm3_SOURCE_DIR}/source/m3_parse.c
  ${wasm3_SOURCE_DIR}/source/m3_validate.c
)
add_library(wasm3 STATIC ${WASM3_SOURCES})
target_include_directories(wasm3 PUBLIC ${wasm3_SOURCE_DIR}/source)
target_compile_definitions(wasm3 PUBLIC d_m3HasWASI)
target_link_libraries(g4f PUBLIC wasm3)

add_executable(g4f_tests
  tests/main.cpp
)
target_link_libraries(g4f_tests PRIVATE g4f)

enable_testing()
add_test(NAME g4f_tests COMMAND g4f_tests)
set_tests_properties(g4f_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
