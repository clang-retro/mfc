# libmfcConfig.cmake - clang-retro MFC for CMake (3.13 or later).
#
#   find_package(libmfc REQUIRED)
#   add_executable(app WIN32 app.cpp)
#   target_link_libraries(app PRIVATE libmfc::mfc)
#   libmfc_add_resources(app app.rc)
#
# The targets: libmfc::mfc (ANSI) and libmfc::mfcu (Unicode), static;
# libmfc::mfc_shared and libmfc::mfcu_shared, the shared libraries
# (_AFXDLL); and libmfc::winport, the portable Win32 layer they need,
# for a program that uses it without MFC. libmfc_add_resources(target
# file.rc [include dirs...]) compiles a resource script with
# res2cpp.py (Python 3 and llvm-rc) and adds it to the target.
#
# Part of clang-retro MFC, an independent implementation of the MFC API.
# SPDX-License-Identifier: AGPL-3.0-only

get_filename_component(_libmfc_prefix "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(LIBMFC_INCLUDE_DIRS "${_libmfc_prefix}/include/libmfc/winport" "${_libmfc_prefix}/include/libmfc")
set(LIBMFC_RES2CPP "${_libmfc_prefix}/share/libmfc/res2cpp.py")
set_property(GLOBAL PROPERTY LIBMFC_RES2CPP "${LIBMFC_RES2CPP}")

if(NOT TARGET libmfc::winport)
  include(CMakeFindDependencyMacro)
  find_dependency(Threads)
  if(APPLE)
    set(_libmfc_syslibs "-framework Cocoa" Threads::Threads)
  else()
    set(_libmfc_syslibs ${CMAKE_DL_LIBS} Threads::Threads)
  endif()
  set(_libmfc_lib "${_libmfc_prefix}/lib")
  set(_libmfc_shared "${_libmfc_lib}/libmfc-shared")
  set(_libmfc_so "${CMAKE_SHARED_LIBRARY_SUFFIX}")

  add_library(libmfc::winport STATIC IMPORTED)
  set_target_properties(libmfc::winport PROPERTIES
    IMPORTED_LOCATION "${_libmfc_lib}/libwinport.a"
    INTERFACE_INCLUDE_DIRECTORIES "${LIBMFC_INCLUDE_DIRS}"
    INTERFACE_COMPILE_FEATURES cxx_std_17
    INTERFACE_LINK_LIBRARIES "${_libmfc_syslibs}")

  foreach(_v mfc mfcu)
    set(_defs "")
    if(_v STREQUAL "mfcu")
      set(_defs UNICODE _UNICODE)
    endif()
    set(_shared_defs ${_defs} _AFXDLL)
    add_library(libmfc::${_v} STATIC IMPORTED)
    set_target_properties(libmfc::${_v} PROPERTIES
      IMPORTED_LOCATION "${_libmfc_lib}/lib${_v}.a"
      INTERFACE_COMPILE_DEFINITIONS "${_defs}"
      INTERFACE_LINK_LIBRARIES libmfc::winport)
    # The program's own part (main among it), then the shared libraries.
    add_library(libmfc::${_v}_shared INTERFACE IMPORTED)
    set_target_properties(libmfc::${_v}_shared PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${LIBMFC_INCLUDE_DIRS}"
      INTERFACE_COMPILE_DEFINITIONS "${_shared_defs}"
      INTERFACE_COMPILE_FEATURES cxx_std_17
      INTERFACE_LINK_LIBRARIES
        "${_libmfc_lib}/lib${_v}s.a;${_libmfc_shared}/lib${_v}${_libmfc_so};${_libmfc_shared}/libwinport${_libmfc_so};${_libmfc_syslibs}"
      INTERFACE_LINK_OPTIONS "LINKER:-rpath,${_libmfc_shared}")
  endforeach()
endif()

# Compiles a resource script into C++ and adds it to the target. The
# script's own directory and MFC's include directory are searched; more
# directories may follow the script.
function(libmfc_add_resources target rc)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  get_property(_res2cpp GLOBAL PROPERTY LIBMFC_RES2CPP)
  get_filename_component(_rc "${rc}" ABSOLUTE)
  get_filename_component(_name "${rc}" NAME_WE)
  set(_out "${CMAKE_CURRENT_BINARY_DIR}/${_name}-res.cpp")
  set(_inc "")
  foreach(_d ${ARGN})
    get_filename_component(_d "${_d}" ABSOLUTE)
    list(APPEND _inc -I "${_d}")
  endforeach()
  add_custom_command(OUTPUT "${_out}"
    COMMAND "${CMAKE_COMMAND}" -E env "CXX=${CMAKE_CXX_COMPILER}"
      "${Python3_EXECUTABLE}" "${_res2cpp}" ${_inc} "${_rc}" -o "${_out}"
    DEPENDS "${_rc}"
    COMMENT "Compiling the resources of ${_rc}"
    VERBATIM)
  target_sources(${target} PRIVATE "${_out}")
endfunction()
