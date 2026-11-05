# Vendored third-party code. Each component keeps its licence next to its
# files and is listed in NOTICE. Built without the project's warning flags.

add_library(adari_doctest INTERFACE)
target_include_directories(adari_doctest SYSTEM INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/doctest)

add_library(adari_json INTERFACE)
target_include_directories(adari_json SYSTEM INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/nlohmann_json/include)
