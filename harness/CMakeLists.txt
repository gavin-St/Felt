add_library(
  ompeval
  STATIC
  third_party/ompeval/omp/HandEvaluator.cpp
)

target_include_directories(
  ompeval
  SYSTEM
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/ompeval
)

add_library(
  felt_core
  STATIC
  src/card.cpp
  src/evaluator.cpp
  src/random.cpp
)

target_include_directories(
  felt_core
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(felt_core PUBLIC ompeval)
felt_configure_target(felt_core)

add_library(
  felt_bot_runner
  STATIC
  src/native_bot_runner.cpp
)

target_include_directories(
  felt_bot_runner
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(felt_bot_runner PUBLIC ${CMAKE_DL_LIBS})
felt_configure_target(felt_bot_runner)

add_executable(run_match src/main.cpp)
target_link_libraries(run_match PRIVATE felt_bot_runner)
felt_configure_target(run_match)
