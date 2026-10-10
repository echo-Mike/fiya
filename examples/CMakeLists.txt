
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

  add_custom_command(
      OUTPUT "${GEN_SOURCE}"
      COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/connectors/gen_synthetic.py"
      DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/connectors/gen_synthetic.py"
      COMMENT "Generating synthetic test..."
      VERBATIM
  )

  add_executable(fiya-cyg-synthetic ${GEN_SOURCE})
  target_compile_features(fiya-cyg-synthetic PRIVATE cxx_std_20)
  target_compile_options(fiya-cyg-synthetic PRIVATE -finstrument-functions -gdwarf -g3)
  target_link_libraries(fiya-cyg-synthetic PRIVATE fiya-stub-connector)
  target_link_options(fiya-cyg-synthetic PRIVATE -rdynamic -Wl,--export-dynamic)
else()
    message(WARNING "Python 3 interpreter not found. Skipping synthetic test generation")
endif()
