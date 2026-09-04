add_library(
  ompeval
  STATIC
  third_party/ompeval/omp/HandEvaluator.cpp
)

find_package(ZLIB REQUIRED)

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
  src/equity.cpp
  src/evaluator.cpp
  src/hand_engine.cpp
  src/match.cpp
  src/match_cli.cpp
  src/match_log.cpp
  src/random.cpp
)

target_include_directories(
  felt_core
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(felt_core PUBLIC ompeval ZLIB::ZLIB)
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

add_executable(run_match src/main.cpp src/match_process.cpp)
target_link_libraries(run_match PRIVATE felt_bot_runner felt_core)
felt_configure_target(run_match)

add_executable(replay_match src/replay_main.cpp)
target_link_libraries(replay_match PRIVATE felt_core)
felt_configure_target(replay_match)

add_executable(rerun_match src/rerun_main.cpp)
target_link_libraries(rerun_match PRIVATE felt_bot_runner felt_core)
felt_configure_target(rerun_match)
