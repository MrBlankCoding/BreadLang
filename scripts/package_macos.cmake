cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED BINARY)
  message(FATAL_ERROR "BINARY is required")
endif()

if(NOT EXISTS "${BINARY}")
  message(FATAL_ERROR "Binary not found: ${BINARY}")
endif()

if(NOT DEFINED DIST_DIR)
  set(DIST_DIR "${CMAKE_CURRENT_LIST_DIR}/../dist")
endif()

get_filename_component(DIST_DIR "${DIST_DIR}" ABSOLUTE)
file(MAKE_DIRECTORY "${DIST_DIR}")
file(MAKE_DIRECTORY "${DIST_DIR}/lib")

set(DIST_BIN "${DIST_DIR}/breadlang")
file(COPY_FILE "${BINARY}" "${DIST_BIN}")

execute_process(COMMAND chmod +x "${DIST_BIN}")

function(_is_system_lib path out_var)
  if(path MATCHES "^/System/Library/" OR path MATCHES "^/usr/lib/")
    set(${out_var} TRUE PARENT_SCOPE)
  else()
    set(${out_var} FALSE PARENT_SCOPE)
  endif()
endfunction()

function(_is_homebrew_or_llvm_lib path out_var)
  if(path MATCHES "^/opt/homebrew/" OR path MATCHES "^/usr/local/" OR path MATCHES "LLVM")
    set(${out_var} TRUE PARENT_SCOPE)
  else()
    set(${out_var} FALSE PARENT_SCOPE)
  endif()
endfunction()

function(_get_deps bin out_list)
  execute_process(
    COMMAND otool -L "${bin}"
    OUTPUT_VARIABLE _out
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  string(REPLACE "\n" ";" _lines "${_out}")
  set(_deps "")
  foreach(_line IN LISTS _lines)
    string(STRIP "${_line}" _line_s)
    if(_line_s STREQUAL "")
      continue()
    endif()
    if(_line_s MATCHES "^${bin}:")
      continue()
    endif()
    if(_line_s MATCHES "^([^ ]+) ")
      set(_path "${CMAKE_MATCH_1}")
      list(APPEND _deps "${_path}")
    endif()
  endforeach()
  list(REMOVE_DUPLICATES _deps)
  set(${out_list} "${_deps}" PARENT_SCOPE)
endfunction()

function(_copy_and_patch_one lib_path)
  get_filename_component(_name "${lib_path}" NAME)
  set(_dst "${DIST_DIR}/lib/${_name}")

  if(NOT EXISTS "${_dst}")
    file(COPY_FILE "${lib_path}" "${_dst}")
    execute_process(COMMAND chmod 644 "${_dst}")
  endif()

  execute_process(COMMAND install_name_tool -id "@rpath/${_name}" "${_dst}")
endfunction()

set(_queue "${DIST_BIN}")
set(_seen "")

while(TRUE)
  list(LENGTH _queue _qlen)
  if(_qlen EQUAL 0)
    break()
  endif()

  list(POP_FRONT _queue _cur)
  list(FIND _seen "${_cur}" _idx)
  if(NOT _idx EQUAL -1)
    continue()
  endif()
  list(APPEND _seen "${_cur}")

  _get_deps("${_cur}" _deps)

  foreach(_dep IN LISTS _deps)
    _is_system_lib("${_dep}" _sys)
    if(_sys)
      continue()
    endif()

    _is_homebrew_or_llvm_lib("${_dep}" _hb)
    if(NOT _hb)
      continue()
    endif()

    if(EXISTS "${_dep}")
      _copy_and_patch_one("${_dep}")
      get_filename_component(_dep_name "${_dep}" NAME)
      set(_local "${DIST_DIR}/lib/${_dep_name}")
      list(APPEND _queue "${_local}")
    endif()
  endforeach()
endwhile()

execute_process(COMMAND install_name_tool -add_rpath "@executable_path/lib" "${DIST_BIN}")

function(_patch_links target)
  _get_deps("${target}" _deps)
  foreach(_dep IN LISTS _deps)
    _is_system_lib("${_dep}" _sys)
    if(_sys)
      continue()
    endif()
    get_filename_component(_name "${_dep}" NAME)
    set(_candidate "${DIST_DIR}/lib/${_name}")
    if(EXISTS "${_candidate}")
      execute_process(COMMAND install_name_tool -change "${_dep}" "@rpath/${_name}" "${target}")
    endif()
  endforeach()
endfunction()

_patch_links("${DIST_BIN}")

file(GLOB _bundled_libs "${DIST_DIR}/lib/*.dylib")
foreach(_lib IN LISTS _bundled_libs)
  execute_process(COMMAND install_name_tool -add_rpath "@executable_path/lib" "${_lib}")
  _patch_links("${_lib}")
endforeach()

message(STATUS "Created bundle at: ${DIST_DIR}")
