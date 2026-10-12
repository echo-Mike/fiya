function(get_system_include_dirs OUT_VAR)
    if(WIN32)
        set(_null "NUL")
    else()
        set(_null "/dev/null")
    endif()

    # GCC and Clang both print the include search list on stderr when
    # invoked as:  <cxx> -x c++ -E -v /dev/null
    execute_process(
        COMMAND "${CMAKE_CXX_COMPILER}" -x c++ -E -v "${_null}"
        OUTPUT_QUIET
        ERROR_VARIABLE _cc_out
        RESULT_VARIABLE _rc
    )
    if(NOT _rc EQUAL 0)
        message(WARNING "get_instrument_exclude_list: compiler query failed (rc=${_rc})")
        set(${OUT_VAR} "" PARENT_SCOPE)
        return()
    endif()

    # Extract the block between the two known markers.
    if(NOT _cc_out MATCHES
       "#include <\\.\\.\\.> search starts here:(.*)End of search list\\.")
        message(WARNING "get_instrument_exclude_list: could not parse compiler output")
        set(${OUT_VAR} "" PARENT_SCOPE)
        return()
    endif()
    set(_block "${CMAKE_MATCH_1}")

    # Each line looks like " /usr/include/c++/13" (leading space).
    string(REPLACE "\n" ";" _lines "${_block}")

    set(_fragments "")
    foreach(line IN LISTS _lines)
        string(STRIP "${line}" line)
        if(line AND IS_DIRECTORY "${line}")
            list(APPEND _fragments "${line}")
        endif()
    endforeach()

    # Belt-and-braces: well-known fragments that appear in the *paths of
    # files inside* the std library. These catch headers even when the
    # directory itself wasn't in the search list (e.g. libc++ on macOS).
    list(APPEND _fragments
        "/usr/lib/gcc"
        "/include/c++/"
        "/bits/stl"
        "bits/"
        "/bits/basic_string"
    )

    list(REMOVE_DUPLICATES _fragments)
    list(JOIN _fragments "," _joined)
    set(${OUT_VAR} "${_joined}" PARENT_SCOPE)
endfunction()

add_executable(fiya-time-measure fiya-time-measure.cpp)
target_compile_features(fiya-time-measure PRIVATE cxx_std_20)

add_executable(fiya-heap-measure
  fiya-heap-measure.cpp
  ${CMAKE_CURRENT_LIST_DIR}/../fiya-heap-overloads.cpp)
target_compile_features(fiya-heap-measure PRIVATE cxx_std_20)

add_executable(fiya-cyg-time-measure
  fiya-cyg-time-measure.cpp 
  ${CMAKE_CURRENT_LIST_DIR}/../fiya-cyg-overloads.cpp)
target_compile_features(fiya-cyg-time-measure PRIVATE cxx_std_20)
target_compile_options(fiya-cyg-time-measure PRIVATE -finstrument-functions -gdwarf -g3)
target_link_options(fiya-cyg-time-measure PRIVATE -rdynamic -Wl,--export-dynamic)
target_link_libraries(fiya-cyg-time-measure PRIVATE ${CMAKE_DL_LIBS})

add_library(fiya-connector-interface INTERFACE)
target_include_directories(fiya-connector-interface INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/connectors/include)

add_library(fiya-stub-connector SHARED ${CMAKE_CURRENT_SOURCE_DIR}/connectors/src/fiya-stub-connector.cpp)
target_compile_features(fiya-stub-connector PRIVATE cxx_std_20)
target_link_libraries(fiya-stub-connector PUBLIC fiya-connector-interface)

find_package(Threads REQUIRED)

add_library(fiya-superfast-connector SHARED ${CMAKE_CURRENT_SOURCE_DIR}/connectors/src/fiya-superfast-connector.cpp)
target_include_directories(fiya-superfast-connector PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(fiya-superfast-connector PRIVATE cxx_std_20)
target_compile_definitions(fiya-superfast-connector PRIVATE
  FIYA_LD_PRELOAD
  FIYA_NO_INTERNAL_PROTECT
  FIYA_NO_EXTERN_RECORDER
  FIYA_USE_FINAL_RECORDER
  )
target_compile_options(fiya-superfast-connector PRIVATE
  -fno-instrument-functions
  -fvisibility=hidden
  -ffast-math
  -fno-math-errno
  )
target_link_libraries(fiya-superfast-connector PUBLIC
  fiya-connector-interface
  Threads::Threads
  )
# target_compile_options(fiya-superfast-connector PRIVATE -flto)
# target_link_options(fiya-superfast-connector PRIVATE -flto)
# target_link_options(fiya-superfast-connector PRIVATE -Wl,-Bstatic -lm -Wl,-Bdynamic)
# target_compile_options(fiya-superfast-connector PRIVATE -gdwarf -g3)

find_package(Python3 COMPONENTS Interpreter)

if(Python3_Interpreter_FOUND)
  set(GEN_SOURCE ${CMAKE_CURRENT_BINARY_DIR}/synthetic.cpp)

  get_system_include_dirs(SYS_INCLUDES)

  add_custom_command(
      OUTPUT "${GEN_SOURCE}"
      COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/connectors/gen_synthetic.py --levels 7 --fanout 7 --workload light
      DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/connectors/gen_synthetic.py"
      COMMENT "Generating synthetic test..."
      VERBATIM
  )

  add_executable(fiya-cyg-synthetic ${GEN_SOURCE})
  target_compile_features(fiya-cyg-synthetic PRIVATE cxx_std_20)
  # We don't instrument std functions here as the have x100 (light) to x450 (heavy) load and make benchmarking unpredictable
  target_compile_options(fiya-cyg-synthetic PRIVATE 
    -finstrument-functions
    -finstrument-functions-exclude-file-list=${SYS_INCLUDES}
    -gdwarf
    -g3)
  target_link_libraries(fiya-cyg-synthetic PRIVATE fiya-stub-connector)
  target_link_options(fiya-cyg-synthetic PRIVATE -rdynamic -Wl,--export-dynamic)
else()
    message(WARNING "Python 3 interpreter not found. Skipping synthetic test generation")
endif()
