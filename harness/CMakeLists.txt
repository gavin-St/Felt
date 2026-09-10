add_library(
  ompeval
  STATIC
  third_party/ompeval/omp/HandEvaluator.cpp
)

set_target_properties(ompeval PROPERTIES POSITION_INDEPENDENT_CODE ON)

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
  felt_botkit
  STATIC
  src/bot_kit.cpp
  src/preflop_chart.cpp
)

target_include_directories(
  felt_botkit
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(felt_botkit PUBLIC ompeval)
set_target_properties(felt_botkit PROPERTIES POSITION_INDEPENDENT_CODE ON)
felt_configure_target(felt_botkit)

add_library(
  felt_bot_runner
  STATIC
  src/bot_loader.cpp
  src/native_bot_runner.cpp
  src/wasm_bot_runner.cpp
)

target_include_directories(
  felt_bot_runner
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(felt_bot_runner PUBLIC ${CMAKE_DL_LIBS})

set(FELT_HAS_WASMTIME OFF)
if(FELT_ENABLE_WASM)
  find_path(
    FELT_WASMTIME_INCLUDE_DIR
    wasmtime.h
    HINTS "${FELT_WASMTIME_ROOT}/include"
    NO_DEFAULT_PATH
  )
  if(EXISTS "${FELT_WASMTIME_ROOT}/lib/libwasmtime.a")
    set(FELT_WASMTIME_LIBRARY "${FELT_WASMTIME_ROOT}/lib/libwasmtime.a")
  else()
    find_library(
      FELT_WASMTIME_LIBRARY
      NAMES wasmtime
      HINTS "${FELT_WASMTIME_ROOT}/lib"
      NO_DEFAULT_PATH
    )
  endif()
  if(FELT_WASMTIME_INCLUDE_DIR AND FELT_WASMTIME_LIBRARY)
    set(FELT_HAS_WASMTIME ON)
    target_include_directories(
      felt_bot_runner SYSTEM PRIVATE "${FELT_WASMTIME_INCLUDE_DIR}"
    )
    target_link_libraries(felt_bot_runner PUBLIC "${FELT_WASMTIME_LIBRARY}")
    message(STATUS "WebAssembly bots enabled with ${FELT_WASMTIME_LIBRARY}")
  else()
    message(
      STATUS
      "WebAssembly bots disabled: run scripts/bootstrap_wasm.py, then reconfigure"
    )
  endif()
endif()

set(FELT_HAS_WASMTIME ${FELT_HAS_WASMTIME} CACHE INTERNAL "Wasmtime found" FORCE)

target_compile_definitions(
  felt_bot_runner PRIVATE FELT_HAS_WASMTIME=$<BOOL:${FELT_HAS_WASMTIME}>
)
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
