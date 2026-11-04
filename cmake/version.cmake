# Writes OUT with the git description of SRC, only when it changed (so nothing rebuilds needlessly).
execute_process(COMMAND git describe --tags --always --dirty WORKING_DIRECTORY ${SRC}
                OUTPUT_VARIABLE describe OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT describe)
    set(describe "unknown")
endif()
set(content "#pragma once\n#define QP_GIT_VERSION \"${describe}\"\n")
if(EXISTS ${OUT})
    file(READ ${OUT} old)
endif()
if(NOT "${old}" STREQUAL "${content}")
    file(WRITE ${OUT} "${content}")
endif()
