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

function(_resolve_dep_path dep_path out_var)
  set(_resolved "")
  get_filename_component(_dep_name "${dep_path}" NAME)
  set(_bundled_candidate "${DIST_DIR}/lib/${_dep_name}")
  if(EXISTS "${_bundled_candidate}")
    set(${out_var} "${_bundled_candidate}" PARENT_SCOPE)
    return()
  endif()
  if(dep_path MATCHES "^@rpath/.*")
    set(_candidates "")

    if(DEFINED LLVM_BUNDLE_SOURCE_LIBDIR AND EXISTS "${LLVM_BUNDLE_SOURCE_LIBDIR}/${_dep_name}")
      list(APPEND _candidates "${LLVM_BUNDLE_SOURCE_LIBDIR}/${_dep_name}")
    endif()

    list(APPEND _candidates
      "/opt/homebrew/opt/llvm/lib/${_dep_name}"
      "/opt/homebrew/lib/${_dep_name}"
      "/usr/local/opt/llvm/lib/${_dep_name}"
      "/usr/local/lib/${_dep_name}"
    )

    foreach(_cand IN LISTS _candidates)
      if(EXISTS "${_cand}")
        set(${out_var} "${_cand}" PARENT_SCOPE)
        return()
      endif()
    endforeach()
  endif()

  if(EXISTS "${dep_path}")
    set(${out_var} "${dep_path}" PARENT_SCOPE)
  else()
    set(${out_var} "" PARENT_SCOPE)
  endif()
endfunction()

function(_copy_and_patch_one lib_path)
  get_filename_component(_name "${lib_path}" NAME)
  set(_dst "${DIST_DIR}/lib/${_name}")
  if(_name STREQUAL "libLLVM-C.dylib")
    get_filename_component(LLVM_BUNDLE_SOURCE_LIBDIR "${lib_path}" DIRECTORY)
    set(LLVM_BUNDLE_SOURCE_LIBDIR "${LLVM_BUNDLE_SOURCE_LIBDIR}" PARENT_SCOPE)
  endif()

  if(NOT EXISTS "${_dst}")
    file(COPY_FILE "${lib_path}" "${_dst}")
    execute_process(COMMAND chmod 644 "${_dst}")
  endif()

  execute_process(COMMAND install_name_tool -id "@rpath/${_name}" "${_dst}")
endfunction()

function(_has_rpath target rpath out_var)
  execute_process(
    COMMAND otool -l "${target}"
    OUTPUT_VARIABLE _ot
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  if(_ot MATCHES "path ${rpath} ")
    set(${out_var} TRUE PARENT_SCOPE)
  else()
    set(${out_var} FALSE PARENT_SCOPE)
  endif()
endfunction()

function(_adhoc_codesign target)
  execute_process(
    COMMAND codesign --remove-signature "${target}"
    RESULT_VARIABLE _rm_rv
    OUTPUT_VARIABLE _rm_out
    ERROR_VARIABLE _rm_err
  )

  execute_process(
    COMMAND codesign --force --sign - --timestamp=none "${target}"
    RESULT_VARIABLE _cs_rv
    OUTPUT_VARIABLE _cs_out
    ERROR_VARIABLE _cs_err
  )
  if(NOT _cs_rv EQUAL 0)
    message(FATAL_ERROR "codesign failed for ${target}: ${_cs_err}")
  endif()
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
    _resolve_dep_path("${_dep}" _dep_resolved)
    if(_dep_resolved STREQUAL "")
      continue()
    endif()

    _is_system_lib("${_dep_resolved}" _sys)
    if(_sys)
      continue()
    endif()

    _is_homebrew_or_llvm_lib("${_dep_resolved}" _hb)
    if(NOT _hb)
      continue()
    endif()

    if(EXISTS "${_dep_resolved}")
      _copy_and_patch_one("${_dep_resolved}")
      get_filename_component(_dep_name "${_dep_resolved}" NAME)
      set(_local "${DIST_DIR}/lib/${_dep_name}")
      list(APPEND _queue "${_local}")
    endif()
  endforeach()
endwhile()

_has_rpath("${DIST_BIN}" "@executable_path/lib" _bin_has_rpath)
if(NOT _bin_has_rpath)
  execute_process(COMMAND install_name_tool -add_rpath "@executable_path/lib" "${DIST_BIN}")
endif()

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
  _has_rpath("${_lib}" "@executable_path/lib" _lib_has_rpath)
  if(NOT _lib_has_rpath)
    execute_process(COMMAND install_name_tool -add_rpath "@executable_path/lib" "${_lib}")
  endif()
  _patch_links("${_lib}")
endforeach()

# install_name_tool modifies binaries and invalidates signatures. Re-sign the bundle.
_adhoc_codesign("${DIST_BIN}")
foreach(_lib IN LISTS _bundled_libs)
  _adhoc_codesign("${_lib}")
endforeach()

message(STATUS "Created bundle at: ${DIST_DIR}")
