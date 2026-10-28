function(run)
    execute_process(COMMAND ${ARGV} RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "Installed-package check (${ARGV0}):\n${output}\n${error}")
    endif()
endfunction()

# Fresh staging avoids finding stale headers/exports left by an earlier install.
string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef instance)
set(stage "${WORK}/package-${instance}")
run("${CMAKE_COMMAND}" --install "${BUILD}" --prefix "${stage}/install" --config "${CONFIG}")
set(extra)
if(PLATFORM)
    list(APPEND extra -A "${PLATFORM}")
endif()
if(TOOLSET)
    list(APPEND extra -T "${TOOLSET}")
endif()
if(TOOLCHAIN)
    list(APPEND extra "-DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN}" -DVCPKG_MANIFEST_MODE=OFF)
endif()
run("${CMAKE_COMMAND}" -S "${SOURCE}" -B "${stage}/build" -G "${GENERATOR}"
    "-DCMAKE_CXX_COMPILER=${COMPILER}" "-DCMAKE_MAKE_PROGRAM=${MAKE_PROGRAM}"
    "-DCMAKE_BUILD_TYPE=${CONFIG}" "-DCMAKE_CXX_FLAGS=${CXX_FLAGS}"
    # Only this configuration was installed. In particular, vcpkg maps the
    # other release configurations to Release, which a Debug-only stage lacks.
    "-DCMAKE_CONFIGURATION_TYPES=${CONFIG}"
    "-DBoost_DIR=${BOOST_CONFIG}"
    "-DPACKAGE_CONFIG_DIR=${stage}/install/${LIBDIR}/cmake/mdcomp" ${extra})
run("${CMAKE_COMMAND}" --build "${stage}/build" --config "${CONFIG}" --parallel 2)
run("${CTEST}" --test-dir "${stage}/build" -C "${CONFIG}" --output-on-failure --no-tests=error)
