find_package(Catch2 3 REQUIRED CONFIG)
include(Catch)

add_executable(mdcomp-unit-tests unit/bigendian_io.cc unit/endian_streams.cc)
target_link_libraries(mdcomp-unit-tests PRIVATE mdcomp::bigendian_io Catch2::Catch2WithMain)
target_include_directories(mdcomp-unit-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

catch_discover_tests(mdcomp-unit-tests
    TEST_PREFIX "unit::"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    PROPERTIES LABELS "unit" TIMEOUT 30
    DISCOVERY_MODE PRE_TEST
)
