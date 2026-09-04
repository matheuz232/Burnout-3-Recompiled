if(NOT DEFINED PROBE)
  message(FATAL_ERROR "PROBE is required")
endif()
if(NOT DEFINED OUTPUT)
  message(FATAL_ERROR "OUTPUT is required")
endif()

file(REMOVE "${OUTPUT}")

execute_process(
  COMMAND "${PROBE}" --seconds 1 --output "${OUTPUT}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE stdout_text
  ERROR_VARIABLE stderr_text
)

if(NOT result EQUAL 0)
  message(FATAL_ERROR "Burnout3PacingProbe failed with ${result}: ${stderr_text}")
endif()

if(NOT EXISTS "${OUTPUT}")
  message(FATAL_ERROR "Burnout3PacingProbe did not create the requested output file")
endif()

file(READ "${OUTPUT}" content)
foreach(marker
    "B3R_PACING_PROBE 1"
    "TARGET_HZ 120"
    "REQUESTED_SECONDS 1"
    "FRAMES 120"
    "B3R_FRAME_PACING_TELEMETRY 1"
    "SAMPLES 120"
    "HIGH_RESOLUTION_TIMER ")
  string(FIND "${content}" "${marker}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "output report is missing marker: ${marker}")
  endif()
endforeach()

if(NOT stdout_text STREQUAL "")
  message(FATAL_ERROR "file-output mode must not duplicate the report to stdout")
endif()

file(REMOVE "${OUTPUT}")
