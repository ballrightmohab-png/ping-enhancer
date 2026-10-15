cmake_minimum_required(VERSION 3.22)

project(levifsr_ai LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------------------
# Package identity
# ---------------------------------------------------------------------------
set(LEVIFSR_MOD_ID "levifsr_ai" CACHE STRING "LeviLaunchroid mod package directory name")
set(LEVIFSR_MOD_NAME "LeviFSR AI" CACHE STRING "LeviLaunchroid mod display name")
set(LEVIFSR_MOD_AUTHOR "LeviFSR AI" CACHE STRING "LeviLaunchroid mod author")
set(LEVIFSR_MOD_VERSION "1.0.0" CACHE STRING "LeviLaunchroid mod version")
set(LEVIFSR_LIBRARY_NAME "levifsr_ai" CACHE STRING "Native library name without the lib prefix")
set(LEVIFSR_MINECRAFT_VERSIONS "" CACHE STRING "Raw JSON array for manifest minecraft_versions")

option(LEVIFSR_BUILD_TESTS "Build the host unit tests" ON)
option(LEVIFSR_LINK_PRELOADER_STUB "Link a build-time stub of libpreloader.so" ON)

set(LEVIFSR_SOURCES
    src/Mod.cpp
    src/RenderPipeline.cpp
    src/GlApi.cpp
    src/AiBackend.cpp
    src/DynamicResolution.cpp
)

# ---------------------------------------------------------------------------
# Host build: unit tests and the configuration generator.
# ---------------------------------------------------------------------------
if(NOT ANDROID)
    # Host unit tests: everything that does not need a GPU.
    add_executable(levifsr_tests
        tests/test_main.cpp
        src/DynamicResolution.cpp
        src/AiBackend.cpp
    )
    target_include_directories(levifsr_tests PRIVATE
        src
        android_stubs
    )
    target_compile_options(levifsr_tests PRIVATE
        $<$<COMPILE_LANG_AND_ID:CXX,Clang,GNU>:-Wall -Wextra -Wpedantic>
    )
    target_compile_definitions(levifsr_tests PRIVATE LEVIFSR_HOST_BUILD)

    add_custom_target(test
        COMMAND levifsr_tests
        DEPENDS levifsr_tests
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Running the LeviFSR AI host unit tests"
        VERBATIM
    )

    # Generates config/config.json and config/config.schema.json from the C++
    # Config struct, so the packaged defaults cannot drift from the code.
    # pl/Config.hpp needs nlohmann/json, boost.pfr, magic_enum and fmt.
    include(FetchContent)
    set(FMT_DOC OFF CACHE BOOL "" FORCE)
    set(FMT_TEST OFF CACHE BOOL "" FORCE)
    set(FMT_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.3)
    FetchContent_Declare(boost_pfr
        GIT_REPOSITORY https://github.com/boostorg/pfr.git
        GIT_TAG boost-1.88.0)
    FetchContent_Declare(magic_enum
        GIT_REPOSITORY https://github.com/Neargye/magic_enum.git
        GIT_TAG v0.9.7)
    FetchContent_Declare(fmt
        GIT_REPOSITORY https://github.com/fmtlib/fmt.git
        GIT_TAG 11.2.0)
    FetchContent_MakeAvailable(nlohmann_json boost_pfr magic_enum fmt)

    add_executable(levifsr_config_generator
        src/config_generator.cpp
    )
    target_include_directories(levifsr_config_generator PRIVATE
        src
        android_stubs
        third_party/preloader_headers
    )
    target_compile_definitions(levifsr_config_generator PRIVATE
        PL_CONFIG_NO_RUNTIME FMT_HEADER_ONLY)
    target_compile_options(levifsr_config_generator PRIVATE
        $<$<COMPILE_LANG_AND_ID:CXX,Clang,GNU>:-Wall -Wextra>
    )
    target_link_libraries(levifsr_config_generator PRIVATE
        nlohmann_json::nlohmann_json
        Boost::pfr
        magic_enum::magic_enum
        fmt::fmt)

    add_custom_target(generate_config
        COMMAND levifsr_config_generator "${CMAKE_BINARY_DIR}/generated-config"
        DEPENDS levifsr_config_generator
        COMMENT "Generating config.json and config.schema.json"
        VERBATIM
    )

    return()
endif()

# ---------------------------------------------------------------------------
# Android build (arm64-v8a)
# ---------------------------------------------------------------------------
if(NOT CMAKE_ANDROID_ARCH_ABI STREQUAL "arm64-v8a")
    message(WARNING
        "LeviFSR AI targets arm64-v8a. "
        "Current ABI is ${CMAKE_ANDROID_ARCH_ABI}.")
endif()

# The C++ standard library has to be the shared NDK runtime: libpreloader.so
# is built with the NDK's libc++, and mixing the static and the shared runtime
# across the module boundary would give two incompatible copies of it.
if(NOT DEFINED ANDROID_STL)
    set(ANDROID_STL "c++_shared")
endif()
message(STATUS "LeviFSR AI: ANDROID_STL=${ANDROID_STL}")

find_library(LEVIFSR_LOG_LIB log REQUIRED)

add_library(levifsr_ai SHARED ${LEVIFSR_SOURCES})
set_target_properties(levifsr_ai PROPERTIES OUTPUT_NAME "${LEVIFSR_LIBRARY_NAME}")

target_include_directories(levifsr_ai PRIVATE
    src
    third_party/preloader_headers
    third_party/khronos
)
target_compile_options(levifsr_ai PRIVATE
    $<$<COMPILE_LANG_AND_ID:CXX,Clang,GNU>:-Wall -Wextra -Wpedantic
    -fvisibility=hidden -fvisibility-inlines-hidden>
    # The preloader SDK structs use designated initializers on members that
    # all have default member initializers, so the omitted members are
    # properly default constructed. GCC still warns about them.
    $<$<COMPILE_LANG_AND_ID:CXX,GNU>:-Wno-missing-field-initializers>
)
target_compile_definitions(levifsr_ai PRIVATE LEVIFSR_ANDROID_BUILD)

target_link_libraries(levifsr_ai PRIVATE "${LEVIFSR_LOG_LIB}")

if(LEVIFSR_LINK_PRELOADER_STUB)
    # The real libpreloader.so is injected into the game process by
    # LeviLaunchroid. This stub exists only so the module links with
    # DT_NEEDED libpreloader.so and correctly mangled undefined pl:: symbols;
    # none of its functions is ever executed on the device.
    add_library(preloader SHARED preloader_stub/preloader_stub.cpp)
    target_include_directories(preloader PRIVATE third_party/preloader_headers)
    set_target_properties(preloader PROPERTIES
        OUTPUT_NAME "preloader"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/stub"
    )
    target_link_libraries(levifsr_ai PRIVATE preloader)
else()
    target_link_options(levifsr_ai PRIVATE -Wl,--unresolved-symbols=ignore-all)
endif()

set_target_properties(levifsr_ai PROPERTIES
    LINK_FLAGS "-Wl,--gc-sections"
)

if(ANDROID_ABI)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/out/${ANDROID_ABI}")
endif()
