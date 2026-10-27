file(MAKE_DIRECTORY "${WORK}")
file(WRITE "${WORK}/input.bin" "test")
set(output "${WORK}/output.bin")

execute_process(COMMAND "${RUNNER}" comper "${WORK}/input.bin" "${output}"
    --image "${IMAGES}/copy68k.bin" --output-size 4 --json
    RESULT_VARIABLE status OUTPUT_VARIABLE report ERROR_VARIABLE error)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "68000 runner: ${error}")
endif()
string(JSON cycles GET "${report}" cycles)
string(JSON reads GET "${report}" bus_reads)
string(JSON writes GET "${report}" bus_writes)
string(JSON model GET "${report}" timing_model)
if(NOT cycles EQUAL 112 OR NOT reads EQUAL 19 OR NOT writes EQUAL 4 OR NOT model STREQUAL "musashi-logical-16-bit-transfers")
    message(FATAL_ERROR "Unexpected 68000 timing: ${report}")
endif()
file(READ "${output}" decoded)
if(NOT decoded STREQUAL "test")
    message(FATAL_ERROR "68000 output mismatch")
endif()

execute_process(COMMAND "${RUNNER}" comper "${WORK}/input.bin" "${output}"
    --image "${IMAGES}/copy68k.bin" --output-limit 64 --json
    RESULT_VARIABLE status OUTPUT_VARIABLE report ERROR_VARIABLE error)
string(JSON size GET "${report}" output_bytes)
if(NOT status EQUAL 0 OR NOT size EQUAL 4)
    message(FATAL_ERROR "Capacity-limited runner: ${error} ${report}")
endif()
file(SIZE "${output}" size)
if(NOT size EQUAL 4)
    message(FATAL_ERROR "Runner leaked unused poisoned output capacity")
endif()

execute_process(COMMAND "${RUNNER}" kosinski-z80 "${WORK}/input.bin" "${output}"
    --image "${IMAGES}/copyz80.bin" --output-size 4 --json
    RESULT_VARIABLE status OUTPUT_VARIABLE report ERROR_VARIABLE error)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "Z80 runner: ${error}")
endif()
string(JSON timing GET "${report}" timing)
if(NOT timing STREQUAL "99 T-states")
    message(FATAL_ERROR "Unexpected Z80 timing: ${report}")
endif()

file(WRITE "${output}" "keep")
foreach(option "--cycle-limit;1" "--output-size;3" "--output-size;-1")
    execute_process(COMMAND "${RUNNER}" comper "${WORK}/input.bin" "${output}"
        --image "${IMAGES}/copy68k.bin" ${option}
        RESULT_VARIABLE status OUTPUT_VARIABLE report ERROR_VARIABLE error)
    file(READ "${output}" decoded)
    if(status EQUAL 0 OR NOT decoded STREQUAL "keep")
        message(FATAL_ERROR "Failed run replaced the output: ${option} ${error}")
    endif()
endforeach()
execute_process(COMMAND "${RUNNER}" comper "${WORK}/input.bin" "${WORK}/input.bin"
    --image "${IMAGES}/copy68k.bin" RESULT_VARIABLE status OUTPUT_VARIABLE report ERROR_VARIABLE error)
file(READ "${WORK}/input.bin" input)
if(status EQUAL 0 OR NOT input STREQUAL "test")
    message(FATAL_ERROR "Runner did not reject destructive input/output alias")
endif()
