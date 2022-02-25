include(CTest)

option(USERVER_FEATURE_TESTSUITE "Enable testsuite targets" ON)

add_custom_target(testsuite-venv-all ALL)

function(userver_venv_setup)
  set(options)
  set(oneValueArgs NAME PYTHON_OUTPUT_VAR)
  set(multiValueArgs REQUIREMENTS)

  cmake_parse_arguments(
    ARG "${options}" "${oneValueArgs}" "${multiValueArgs}"  ${ARGN})

  if (NOT ARG_REQUIREMENTS)
    message(FATAL_ERROR "No REQUIREMENTS given for venv")
    return()
  endif()

  if (NOT ARG_NAME)
    set(VENV_NAME "venv")
    set(PYTHON_OUTPUT_VAR "TESTSUITE_VENV_PYTHON")
  else()
    set(VENV_NAME "venv-${ARG_NAME}")
    string(TOUPPER "TESTSUITE_VENV_${ARG_NAME}_PYTHON" PYTHON_OUTPUT_VAR)
  endif()

  if (ARG_PYTHON_OUTPUT_VAR)
    set(PYTHON_OUTPUT_VAR ${ARG_PYTHON_OUTPUT_VAR})
  endif()

  find_program(TESTSUITE_VIRTUALENV virtualenv)
  if (${TESTSUITE_VIRTUALENV} STREQUAL "TESTSUITE_VIRTUALENV-NOTFOUND")
    message(FATAL_ERROR
      "No virtualenv binary found, try to install:\n"
      "Debian: sudo apt install virtualenv\n"
      "MacOS: brew install virtualenv")
  endif()

  set(VENV_DIR ${CMAKE_CURRENT_BINARY_DIR}/${VENV_NAME})
  set(VENV_BIN_DIR ${VENV_DIR}/bin)
  set(${PYTHON_OUTPUT_VAR} ${VENV_BIN_DIR}/python PARENT_SCOPE)

  add_custom_command(
    OUTPUT ${VENV_DIR}
    COMMAND ${TESTSUITE_VIRTUALENV} --python=${PYTHON} ${VENV_DIR}
  )
  add_custom_command(
    OUTPUT ${VENV_DIR}/.stamp
    DEPENDS
    ${VENV_DIR}
    ${ARG_REQUIREMENTS}
    COMMAND
    ${VENV_BIN_DIR}/pip install -U -r ${ARG_REQUIREMENTS}
    COMMAND touch ${VENV_DIR}/.stamp
  )
  add_custom_target(
    testsuite-${VENV_NAME}
    COMMENT "Creating testsuite virtualenv ${VENV_NAME}"
    DEPENDS ${VENV_DIR}/.stamp
  )
  add_dependencies(testsuite-venv-all testsuite-${VENV_NAME})
endfunction()

function(userver_testsuite_add)
  set(options)
  set(oneValueArgs NAME WORKING_DIRECTORY VENV)
  set(multiValueArgs PYTEST_ARGS REQUIREMENTS)
  cmake_parse_arguments(
    ARG "${options}" "${oneValueArgs}" "${multiValueArgs}"  ${ARGN})

  if (NOT ARG_NAME)
    message(FATAL_ERROR "No NAME given for testsuite")
    return()
  endif()

  if (NOT ARG_WORKING_DIRECTORY)
    set(ARG_WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  endif()

  if (NOT USERVER_FEATURE_TESTSUITE)
    message(STATUS "Testsuite target ${ARG_NAME} is disabled")
    return()
  endif()

  if (ARG_REQUIREMENTS)
    userver_venv_setup(
      NAME ${ARG_NAME}
      REQUIREMENTS ${ARG_REQUIREMENTS}
      PYTHON_OUTPUT_VAR "TESTSUITE_VENV_PYTHON"
    )
    set(PYTHON_VAR "TESTSUITE_VENV_PYTHON")
  elseif (ARG_VENV)
    string(TOUPPER "TESTSUITE_VENV_${ARG_VENV}_PYTHON" PYTHON_VAR)
  else()
    set(PYTHON_VAR "TESTSUITE_VENV_PYTHON")
  endif()

  add_test(
    NAME ${ARG_NAME}
    COMMAND ${${PYTHON_VAR}} -m pytest
    -vv --build-dir=${CMAKE_BINARY_DIR} ${ARG_PYTEST_ARGS}
    WORKING_DIRECTORY ${ARG_WORKING_DIRECTORY}
  )
endfunction()
