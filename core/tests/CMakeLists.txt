add_executable(adari_tests
    main.cpp
    test_grid.cpp
    test_readers.cpp
    test_report.cpp
    test_rules.cpp)
target_link_libraries(adari_tests PRIVATE
    adari::core adari_doctest adari_json adari_options)
target_compile_definitions(adari_tests PRIVATE
    ADARI_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
    ADARI_TEST_OUTPUT_DIR="${CMAKE_CURRENT_BINARY_DIR}")
add_test(NAME adari_tests COMMAND adari_tests)

# The CLI end to end: a passing sample exits 0, a failing one exits 1, and a
# missing file exits 2.
set(samples ${PROJECT_SOURCE_DIR}/examples)
add_test(NAME cli_pass COMMAND adari check ${samples}/courtyard/courtyard.txt
         --rules ${samples}/courtyard/rules.json --quiet)
add_test(NAME cli_fail COMMAND adari check ${samples}/courtyard/courtyard-broken.txt
         --rules ${samples}/courtyard/rules.json --quiet
         --json ${CMAKE_CURRENT_BINARY_DIR}/broken.json
         --sarif ${CMAKE_CURRENT_BINARY_DIR}/broken.sarif
         --overlay ${CMAKE_CURRENT_BINARY_DIR}/broken.png)
set_tests_properties(cli_fail PROPERTIES WILL_FAIL TRUE)
add_test(NAME cli_missing COMMAND adari check ${samples}/does-not-exist.txt
         --rules ${samples}/courtyard/rules.json --quiet)
set_tests_properties(cli_missing PROPERTIES WILL_FAIL TRUE)
