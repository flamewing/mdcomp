find_package(Catch2 3 REQUIRED CONFIG)
include(Catch)
option(MDCOMP_ASSEMBLY_TESTS "Build ASL and emulator tests (requires pinned submodules)" OFF)
if(MDCOMP_ASSEMBLY_TESTS)
    add_subdirectory(asm)
endif()

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

add_executable(mdcomp-codec-smoke-tests
    codec/lzss_smoke.cc
    codec/contracts.cc
    codec/known_answers.cc
    codec/external_answers.cc
    codec/moduled.cc
    codec/edge_coverage.cc
    fixtures/known_answers.cc
    support/codec_api.cc
    support/format_observer.cc)

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/edges.json")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/edges.json" edge_manifest)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/edge_obligations.inc" "// Generated from fixtures/edges.json.\n")
string(JSON codec_count LENGTH "${edge_manifest}" codecs)
math(EXPR codec_last "${codec_count} - 1")
foreach(index RANGE ${codec_last})
    string(JSON codec MEMBER "${edge_manifest}" codecs ${index})
    foreach(mode encoder decoder boundary)
        string(JSON count LENGTH "${edge_manifest}" codecs "${codec}" "${mode}")
        math(EXPR last "${count} - 1")
        foreach(edge_index RANGE ${last})
            string(JSON edge GET "${edge_manifest}" codecs "${codec}" "${mode}" ${edge_index})
            file(APPEND "${CMAKE_CURRENT_BINARY_DIR}/edge_obligations.inc" "{\"${codec}\", \"${mode}\", \"${edge}\"},\n")
        endforeach()
    endforeach()
endforeach()
target_include_directories(mdcomp-codec-smoke-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(mdcomp-codec-smoke-tests PRIVATE Catch2::Catch2WithMain
    mdcomp::comperStatic mdcomp::comperxStatic mdcomp::kosinskiStatic
    mdcomp::kosplusStatic mdcomp::lzkn1Static mdcomp::rocketStatic mdcomp::saxmanStatic
    mdcomp::enigmaStatic mdcomp::nemesisStatic mdcomp::snkrleStatic)
catch_discover_tests(mdcomp-codec-smoke-tests
    TEST_PREFIX "codec::"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    PROPERTIES LABELS "codec" TIMEOUT 30
    DISCOVERY_MODE PRE_TEST
    ADD_TAGS_AS_LABELS
)

add_executable(mdcomp-fixture-export support/export_fixtures.cc fixtures/known_answers.cc)
target_include_directories(mdcomp-fixture-export PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(mdcomp-fixture-export PRIVATE mdcomp::bigendian_io)
add_test(NAME codec::fixture-manifest
    COMMAND "${CMAKE_COMMAND}" "-DEXPORTER=$<TARGET_FILE:mdcomp-fixture-export>"
        "-DOUTPUT_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}/fixture-export"
        "-DMANIFEST=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/manifest.json"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/support/check_fixtures.cmake")
set_tests_properties(codec::fixture-manifest PROPERTIES LABELS "codec;fixtures" TIMEOUT 30)

add_test(NAME package::installed-codecs COMMAND "${CMAKE_COMMAND}"
    "-DBUILD=${PROJECT_BINARY_DIR}" "-DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/package"
    "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/installed-package" "-DCONFIG=$<CONFIG>"
    "-DGENERATOR=${CMAKE_GENERATOR}" "-DPLATFORM=${CMAKE_GENERATOR_PLATFORM}" "-DTOOLSET=${CMAKE_GENERATOR_TOOLSET}"
    "-DCOMPILER=${CMAKE_CXX_COMPILER}" "-DMAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
    "-DTOOLCHAIN=${CMAKE_TOOLCHAIN_FILE}" "-DBOOST_CONFIG=${Boost_DIR}"
    "-DCXX_FLAGS=${CMAKE_CXX_FLAGS}" "-DLIBDIR=${CMAKE_INSTALL_LIBDIR}" "-DCTEST=${CMAKE_CTEST_COMMAND}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/support/check_package.cmake")
set_tests_properties(package::installed-codecs PROPERTIES LABELS "package" TIMEOUT 180)

set(MDCOMP_REFERENCE_BIN_DIR "" CACHE PATH "Optional pinned reference CLI binary directory")
set(MDCOMP_REFERENCE_COMMIT "unspecified" CACHE STRING "Commit identifying the reference binaries")
if(MDCOMP_REFERENCE_BIN_DIR)
    find_package(Python3 3.6 REQUIRED COMPONENTS Interpreter)
    set(reference_runtime_args)
    if(MSVC)
        get_target_property(getopt_library_type unofficial::getopt-win32::getopt TYPE)
        if(getopt_library_type STREQUAL "SHARED_LIBRARY")
            list(APPEND reference_runtime_args --candidate-runtime-dir
                "$<TARGET_FILE_DIR:unofficial::getopt-win32::getopt>")
        endif()
    endif()
    add_test(NAME regression::master-binaries
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/support/compare_binaries.py"
            --reference-dir "${MDCOMP_REFERENCE_BIN_DIR}"
            --candidate-dir "$<TARGET_FILE_DIR:kosinskicmp>"
            --reference-commit "${MDCOMP_REFERENCE_COMMIT}"
            ${reference_runtime_args}
            --output-dir "${CMAKE_CURRENT_BINARY_DIR}/master-comparison")
    set_tests_properties(regression::master-binaries PROPERTIES
        LABELS "codec;reference" TIMEOUT 600 WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
endif()
