find_package(Catch2 3 REQUIRED CONFIG)
include(Catch)

add_executable(mdcomp-unit-tests
    unit/bigendian_io.cc
    unit/endian_streams.cc
    unit/endian_int128.cc
    unit/function_ref.cc
    unit/small_utilities.cc
    unit/bitstream.cc
    unit/stream_utils.cc
    unit/decoder_wrappers.cc
    unit/lzss.cc
    unit/options.cc
)
target_link_libraries(mdcomp-unit-tests PRIVATE mdcomp::bigendian_io Catch2::Catch2WithMain)
target_include_directories(mdcomp-unit-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
if(MSVC)
    target_link_libraries(mdcomp-unit-tests PRIVATE unofficial::getopt-win32::getopt)
endif()

add_library(mdcomp-header-checks OBJECT support/header_checks.cc)
target_link_libraries(mdcomp-header-checks PRIVATE mdcomp::bigendian_io)
if("cxx_std_23" IN_LIST CMAKE_CXX_COMPILE_FEATURES)
    add_library(mdcomp-header-checks-cxx23 OBJECT support/header_checks_cxx23.cc)
    target_link_libraries(mdcomp-header-checks-cxx23 PRIVATE mdcomp::bigendian_io)
    target_compile_features(mdcomp-header-checks-cxx23 PRIVATE cxx_std_23)
endif()

catch_discover_tests(mdcomp-unit-tests
    TEST_PREFIX "unit::"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    PROPERTIES LABELS "unit" TIMEOUT 30
    DISCOVERY_MODE PRE_TEST
)

add_executable(mdcomp-codec-smoke-tests codec/lzss_smoke.cc)
target_link_libraries(mdcomp-codec-smoke-tests PRIVATE Catch2::Catch2WithMain
    mdcomp::comperStatic mdcomp::comperxStatic mdcomp::kosinskiStatic
    mdcomp::kosplusStatic mdcomp::lzkn1Static mdcomp::rocketStatic mdcomp::saxmanStatic)
catch_discover_tests(mdcomp-codec-smoke-tests
    TEST_PREFIX "codec::"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    PROPERTIES LABELS "codec" TIMEOUT 30
    DISCOVERY_MODE PRE_TEST
)
