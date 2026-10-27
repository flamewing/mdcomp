file(RELATIVE_PATH source_argument "${WORKING_DIRECTORY}" "${SOURCE}")
execute_process(COMMAND "${ASL}" -q -i . -L -olist "${OUTPUT}.lst" -o "${OUTPUT}.p" "${source_argument}"
    WORKING_DIRECTORY "${WORKING_DIRECTORY}" RESULT_VARIABLE assembled OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 30)
if(NOT assembled EQUAL 0)
    message(FATAL_ERROR "ASL failed for ${SOURCE}:\n${stdout}\n${stderr}")
endif()
execute_process(COMMAND "${P2BIN}" "${OUTPUT}.p" "${OUTPUT}.bin" -q
    RESULT_VARIABLE converted OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 30)
if(NOT converted EQUAL 0)
    message(FATAL_ERROR "p2bin failed: ${stdout}\n${stderr}")
endif()
