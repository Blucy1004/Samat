execute_process(
    COMMAND "${PROGRAM}" run "${SCRIPT}"
    INPUT_FILE "${INPUT}"
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
    RESULT_VARIABLE status)

if(NOT status EQUAL 0)
    message(FATAL_ERROR "Samat Runner failed (${status}): ${error}\n${output}")
endif()

string(FIND "${output}" "결과: 49" result_position)
string(FIND "${output}" "THE main.st IS REAL!!!" completion_position)
if(result_position EQUAL -1 OR completion_position EQUAL -1)
    message(FATAL_ERROR "Unexpected calculator output:\n${output}")
endif()
