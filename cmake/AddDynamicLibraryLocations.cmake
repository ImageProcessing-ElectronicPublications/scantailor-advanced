# add_dynamic_lib_locations(<prefix name> <target> ...)
#
# Finds and adds the dynamic library locations for each configuration 
# to the imported unknown target if it is actually a shared library on Windows.
#
# Tries to find the matching dynamic library for each import one in IMPORTED_LOCATION properties
# and saves the result in the <prefix name>_DYNAMIC_LIBRARY_<Config> variable 
# adding IMPORTED_DYNLIB_<Config> property with the actual dynamic library location to the target on success.
#
# If an IMPORTED_LOCATION property points to a non-import library (for example static) 
# or the matching dynamic library not found, no actions are applied to the target.
#
# For shared libraries that have non-standard dynamic libraries locations 
# appropriate <prefix name>_DYNAMIC_LIBRARY_<Config> cache variables should be set
# or the locations should be added to CMAKE_PROGRAM_PATH or CMAKE_PREFIX_PATH variables.
#
# An optional argument can be used to specify a non-standard name for the dynamic library.

function(add_dynamic_library_locations Targets)
  foreach(_target ${Targets})
    get_target_property(target_type ${_target} TYPE)
    if (NOT target_type STREQUAL "UNKNOWN_LIBRARY")
      continue()
    endif()

    set(imported_location_props)
    get_target_property(configurations ${_target} IMPORTED_CONFIGURATIONS)
    foreach(_config ${configurations})
      list(APPEND imported_location_props IMPORTED_LOCATION_${_config})
    endforeach()

    foreach(_prop ${imported_location_props})
      string(REPLACE IMPORTED_LOCATION DYNAMIC_LIBRARY dynamic_lib ${_prop})
      string(REPLACE IMPORTED_LOCATION IMPORTED_DYNLIB dynlib_prop ${_prop})
      get_target_property(imp_loc ${_target} ${_prop})
      if (NOT imp_loc OR NOT EXISTS ${imp_loc})
        continue()
      endif()
      get_filename_component(imp_loc_ext ${imp_loc} EXT)
      if (imp_loc_ext STREQUAL CMAKE_IMPORT_LIBRARY_SUFFIX)
        get_filename_component(lib_name_we ${imp_loc} NAME_WE)
        set(candidate_names
          "${lib_name_we}${CMAKE_SHARED_LIBRARY_SUFFIX}")
        if(DEFINED ARGV1)
          list(APPEND candidate_names ${ARGV1})
        endif()
        find_program(
            ${_target}_${dynamic_lib}
            NAMES  ${candidate_names}
            DOC Path to the dynamic library of the shared target \(skip it if the target is not shared\).
            NO_CACHE)
        if (${_target}_${dynamic_lib})
          set_target_properties(
              ${_target} PROPERTIES
              ${dynlib_prop} ${${_target}_${dynamic_lib}})
        endif()
      endif()
    endforeach()
  endforeach()
endfunction()
