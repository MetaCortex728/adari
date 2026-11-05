cmake_minimum_required(VERSION 3.21)

project(adari
    VERSION 0.1.0
    DESCRIPTION "Checks that game maps do what their designers intended"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ADARI_BUILD_TESTS "Build the Adari unit tests" ON)
option(ADARI_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(ADARI_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

# Shared warning and sanitizer settings, applied per target. Third-party code
# is built without the warning flags but with the sanitizer flags.
add_library(adari_sanitize INTERFACE)
add_library(adari_options INTERFACE)
target_link_libraries(adari_options INTERFACE adari_sanitize)
if(MSVC)
    target_compile_options(adari_options INTERFACE /W4 /permissive- /utf-8)
    if(ADARI_WARNINGS_AS_ERRORS)
        target_compile_options(adari_options INTERFACE /WX)
    endif()
else()
    target_compile_options(adari_options INTERFACE
        -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion)
    # No fused multiply-add contraction, so distances are the same in the last
    # bit on every architecture.
    target_compile_options(adari_sanitize INTERFACE -ffp-contract=off)
    if(ADARI_WARNINGS_AS_ERRORS)
        target_compile_options(adari_options INTERFACE -Werror)
    endif()
    if(ADARI_SANITIZE)
        target_compile_options(adari_sanitize INTERFACE
            -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
        target_link_options(adari_sanitize INTERFACE -fsanitize=address,undefined)
    endif()
endif()

add_subdirectory(third_party)

# The engine-independent core: model, grid algorithms, rules, readers and
# reports. No engine types, so it builds and tests anywhere; engine front ends
# wrap it.
add_library(adari_core STATIC
    core/src/grid.cpp
    core/src/map.cpp
    core/src/readers.cpp
    core/src/report.cpp
    core/src/rules.cpp)
add_library(adari::core ALIAS adari_core)
target_include_directories(adari_core PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/core/include>)
target_compile_definitions(adari_core PRIVATE
    ADARI_VERSION_STRING="${PROJECT_VERSION}")
target_link_libraries(adari_core
    PRIVATE $<BUILD_INTERFACE:adari_json> $<BUILD_INTERFACE:adari_options>)

add_executable(adari cli/main.cpp)
target_link_libraries(adari PRIVATE adari::core adari_options)

if(ADARI_BUILD_TESTS)
    enable_testing()
    add_subdirectory(core/tests)
endif()
