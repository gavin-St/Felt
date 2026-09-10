add_executable(felt_api_layout_test api_layout_test.cpp)
target_link_libraries(felt_api_layout_test PRIVATE felt_bot_runner)
felt_configure_target(felt_api_layout_test)

add_executable(felt_native_bot_runner_test native_bot_runner_test.cpp)
target_link_libraries(felt_native_bot_runner_test PRIVATE felt_bot_runner)
felt_configure_target(felt_native_bot_runner_test)

if(FELT_HAS_WASMTIME AND EXISTS "${FELT_WASI_SDK_ROOT}/bin/clang")
  set(FELT_WASM_TEST_EXPORTS
      -Wl,--export-memory
      -Wl,--export=felt_wasm_bot_abi_version
      -Wl,--export=felt_wasm_state_ptr
      -Wl,--export=felt_wasm_history_ptr
      -Wl,--export=felt_wasm_history_capacity
      -Wl,--export=felt_wasm_action_ptr
      -Wl,--export=felt_wasm_bot_name_ptr
      -Wl,--export=felt_wasm_act)

  function(felt_compile_test_wasm output source)
    add_custom_command(
      OUTPUT "${output}"
      COMMAND
        "${FELT_WASI_SDK_ROOT}/bin/clang"
        --target=wasm32-wasip1
        -O2
        -std=c11
        -mexec-model=reactor
        -I${PROJECT_SOURCE_DIR}/harness/include
        ${FELT_WASM_TEST_EXPORTS}
        -o "${output}"
        "${source}"
        ${PROJECT_SOURCE_DIR}/harness/wasm/wasm_adapter.c
      DEPENDS
        "${source}"
        ${PROJECT_SOURCE_DIR}/harness/wasm/wasm_adapter.c
        ${PROJECT_SOURCE_DIR}/harness/include/felt/bot_api.h
        ${PROJECT_SOURCE_DIR}/harness/include/felt/wasm_bot_api.h
      VERBATIM
    )
  endfunction()

  set(FELT_WASM_CHECK_CALL ${CMAKE_CURRENT_BINARY_DIR}/wasm_check_call.wasm)
  set(FELT_WASM_HANGING ${CMAKE_CURRENT_BINARY_DIR}/wasm_hanging.wasm)
  set(FELT_WASM_IMPORTING ${CMAKE_CURRENT_BINARY_DIR}/wasm_importing.wasm)
  felt_compile_test_wasm(
    "${FELT_WASM_CHECK_CALL}"
    ${CMAKE_CURRENT_SOURCE_DIR}/wasm_check_call.c
  )
  felt_compile_test_wasm(
    "${FELT_WASM_HANGING}"
    ${CMAKE_CURRENT_SOURCE_DIR}/wasm_hanging.c
  )
  felt_compile_test_wasm(
    "${FELT_WASM_IMPORTING}"
    ${CMAKE_CURRENT_SOURCE_DIR}/wasm_importing.c
  )
  add_custom_target(
    felt_wasm_test_bots ALL
    DEPENDS
      "${FELT_WASM_CHECK_CALL}"
      "${FELT_WASM_HANGING}"
      "${FELT_WASM_IMPORTING}"
  )
  add_executable(felt_wasm_bot_runner_test wasm_bot_runner_test.cpp)
  add_dependencies(felt_wasm_bot_runner_test felt_wasm_test_bots)
  target_link_libraries(felt_wasm_bot_runner_test PRIVATE felt_bot_runner)
  felt_configure_target(felt_wasm_bot_runner_test)
endif()

add_executable(felt_reference_bot_strategy_test reference_bot_strategy_test.cpp)
target_link_libraries(felt_reference_bot_strategy_test PRIVATE felt_bot_runner)
felt_configure_target(felt_reference_bot_strategy_test)

add_executable(felt_slp_test slp_test.cpp)
target_link_libraries(felt_slp_test PRIVATE felt_bot_runner)
felt_configure_target(felt_slp_test)

add_executable(
  felt_board_value_test
  board_value_test.cpp
  ${PROJECT_SOURCE_DIR}/bots/board_value.c
)
target_include_directories(
  felt_board_value_test PRIVATE ${PROJECT_SOURCE_DIR}/bots
)
target_link_libraries(felt_board_value_test PRIVATE felt_bot_runner felt_botkit)
felt_configure_target(felt_board_value_test)

add_executable(
  felt_crusher_test
  crusher_test.cpp
  ${PROJECT_SOURCE_DIR}/bots/board_value.c
  ${PROJECT_SOURCE_DIR}/bots/range_read.c
  ${PROJECT_SOURCE_DIR}/bots/bet_sizing.c
  ${PROJECT_SOURCE_DIR}/bots/raise_rules.c
  ${PROJECT_SOURCE_DIR}/bots/call_rules.c
)
target_include_directories(felt_crusher_test PRIVATE ${PROJECT_SOURCE_DIR}/bots)
target_link_libraries(felt_crusher_test PRIVATE felt_bot_runner felt_botkit)
felt_configure_target(felt_crusher_test)

add_executable(felt_archetype_test archetype_test.cpp)
target_link_libraries(felt_archetype_test PRIVATE felt_bot_runner)
felt_configure_target(felt_archetype_test)

add_executable(felt_card_random_test card_random_test.cpp)
target_link_libraries(felt_card_random_test PRIVATE felt_core)
felt_configure_target(felt_card_random_test)

add_executable(felt_evaluator_test evaluator_test.cpp)
target_link_libraries(felt_evaluator_test PRIVATE felt_core)
felt_configure_target(felt_evaluator_test)

add_executable(felt_bot_kit_test bot_kit_test.cpp)
target_link_libraries(felt_bot_kit_test PRIVATE felt_botkit felt_core)
felt_configure_target(felt_bot_kit_test)

add_executable(felt_bot_kit_c_api_test bot_kit_c_api_test.c)
target_link_libraries(felt_bot_kit_c_api_test PRIVATE felt_botkit)
felt_configure_target(felt_bot_kit_c_api_test)

add_executable(felt_equity_test equity_test.cpp)
target_link_libraries(felt_equity_test PRIVATE felt_core)
felt_configure_target(felt_equity_test)

add_executable(felt_hand_engine_test hand_engine_test.cpp)
target_link_libraries(felt_hand_engine_test PRIVATE felt_core)
felt_configure_target(felt_hand_engine_test)

add_executable(felt_match_test match_test.cpp)
target_link_libraries(felt_match_test PRIVATE felt_core)
felt_configure_target(felt_match_test)

add_executable(felt_match_cli_test match_cli_test.cpp)
target_link_libraries(felt_match_cli_test PRIVATE felt_core)
felt_configure_target(felt_match_cli_test)

add_executable(felt_match_log_test match_log_test.cpp)
target_link_libraries(felt_match_log_test PRIVATE felt_core)
felt_configure_target(felt_match_log_test)

add_executable(
  felt_evaluator_crosscheck
  evaluator_crosscheck.cpp
  reference_evaluator.cpp
)
target_link_libraries(felt_evaluator_crosscheck PRIVATE felt_core)
felt_configure_target(felt_evaluator_crosscheck)

add_executable(felt_evaluator_benchmark evaluator_benchmark.cpp)
target_link_libraries(felt_evaluator_benchmark PRIVATE felt_core)
felt_configure_target(felt_evaluator_benchmark)

add_test(NAME api_layout COMMAND felt_api_layout_test)
add_test(
  NAME native_bot_runner
  COMMAND
    felt_native_bot_runner_test
    $<TARGET_FILE:check_fold>
    $<TARGET_FILE:check_call>
    $<TARGET_FILE:always_all_in>
    $<TARGET_FILE:random_randy>
    $<TARGET_FILE:bad_abi>
    $<TARGET_FILE:missing_act>
)
if(TARGET felt_wasm_bot_runner_test)
  add_test(
    NAME wasm_bot_runner
    COMMAND
      felt_wasm_bot_runner_test
      "${FELT_WASM_CHECK_CALL}"
      "${FELT_WASM_HANGING}"
      "${FELT_WASM_IMPORTING}"
  )
  set_tests_properties(wasm_bot_runner PROPERTIES TIMEOUT 10)
endif()
add_test(
  NAME reference_bot_strategy
  COMMAND
    felt_reference_bot_strategy_test
    $<TARGET_FILE:nit_all_in>
    $<TARGET_FILE:better_all_in>
    $<TARGET_FILE:worse_all_in>
    $<TARGET_FILE:solved_all_in>
)
add_test(
  NAME slp
  COMMAND
    felt_slp_test
    $<TARGET_FILE:slp_fold>
    $<TARGET_FILE:slp_bluff>
    $<TARGET_FILE:slp_balance>
    $<TARGET_FILE:slp_exploit_fold>
    $<TARGET_FILE:slp_exploit_solved>
)

add_test(
  NAME board_value
  COMMAND felt_board_value_test $<TARGET_FILE:slp_odds>
)

add_test(
  NAME crusher
  COMMAND felt_crusher_test $<TARGET_FILE:the_crusher>
)

add_test(
  NAME archetype
  COMMAND
    felt_archetype_test
    $<TARGET_FILE:nitty_nancy>
    $<TARGET_FILE:calling_station>
    $<TARGET_FILE:passive_patty>
    $<TARGET_FILE:chasing_charlie>
    $<TARGET_FILE:semi_bluff_sarah>
    $<TARGET_FILE:trapping_thomas>
    $<TARGET_FILE:triple_barrel_travis>
    $<TARGET_FILE:one_and_done>
    $<TARGET_FILE:scared_sam>
    $<TARGET_FILE:tilted_terry>
    $<TARGET_FILE:min_raise_miranda>
    $<TARGET_FILE:overbet_oliver>
    $<TARGET_FILE:aggressive_andy>
    $<TARGET_FILE:check_raise_chalamet>
)
add_test(
  NAME run_match_smoke
  COMMAND
    run_match
    $<TARGET_FILE:always_all_in>
    $<TARGET_FILE:check_fold>
    --hands 2
    --seed 17
    --out ${CMAKE_CURRENT_BINARY_DIR}/run-match-smoke-results
)
set_tests_properties(
  run_match_smoke
  PROPERTIES PASS_REGULAR_EXPRESSION "always-all-in: adjusted_net_chips=150 raw_net_chips=150"
)
add_test(
  NAME replay_match_smoke
  COMMAND replay_match ${CMAKE_CURRENT_BINARY_DIR}/run-match-smoke-results
)
set_tests_properties(
  replay_match_smoke
  PROPERTIES
    DEPENDS run_match_smoke
    PASS_REGULAR_EXPRESSION "verified 2 logged hands"
)
add_test(
  NAME rerun_match_smoke
  COMMAND
    rerun_match
    ${CMAKE_CURRENT_BINARY_DIR}/run-match-smoke-results
    $<TARGET_FILE:always_all_in>
    $<TARGET_FILE:check_fold>
)
set_tests_properties(
  rerun_match_smoke
  PROPERTIES
    DEPENDS run_match_smoke
    PASS_REGULAR_EXPRESSION "hands_different=0 decisions_different=0"
)
if(TARGET felt_wasm_bot_runner_test)
  add_test(
    NAME run_match_wasm_smoke
    COMMAND
      run_match
      "${FELT_WASM_CHECK_CALL}"
      $<TARGET_FILE:check_fold>
      --hands 2
      --seed 29
      --out ${CMAKE_CURRENT_BINARY_DIR}/run-match-wasm-smoke-results
  )
  add_test(
    NAME rerun_match_wasm_smoke
    COMMAND
      rerun_match
      ${CMAKE_CURRENT_BINARY_DIR}/run-match-wasm-smoke-results
      "${FELT_WASM_CHECK_CALL}"
      $<TARGET_FILE:check_fold>
  )
  set_tests_properties(
    rerun_match_wasm_smoke
    PROPERTIES
      DEPENDS run_match_wasm_smoke
      PASS_REGULAR_EXPRESSION "hands_different=0 decisions_different=0"
  )
endif()
add_test(NAME card_random COMMAND felt_card_random_test)
add_test(NAME evaluator COMMAND felt_evaluator_test)
add_test(NAME bot_kit COMMAND felt_bot_kit_test)
add_test(NAME bot_kit_c_api COMMAND felt_bot_kit_c_api_test)
add_test(NAME equity COMMAND felt_equity_test)
add_test(NAME hand_engine COMMAND felt_hand_engine_test)
add_test(NAME match COMMAND felt_match_test)
add_test(NAME match_cli COMMAND felt_match_cli_test)
add_test(
  NAME match_log
  COMMAND
    felt_match_log_test
    ${CMAKE_CURRENT_BINARY_DIR}/match-log-test-output
    $<TARGET_FILE:felt_match_log_test>
)
add_test(NAME evaluator_crosscheck COMMAND felt_evaluator_crosscheck 100000)

find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
  add_test(
    NAME sqlite_scripts
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/test_finalize_match.py
  )
  add_test(
    NAME published_numbers
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/scripts/test_published_numbers.py
  )
  add_test(
    NAME match_workflow
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/test_match_workflow.py
  )
  add_test(
    NAME match_supervision
    COMMAND
      ${Python3_EXECUTABLE}
      ${CMAKE_SOURCE_DIR}/scripts/test_match_supervision.py
      $<TARGET_FILE:run_match>
      $<TARGET_FILE:hanging>
      $<TARGET_FILE:crashing>
      $<TARGET_FILE:check_fold>
  )
  set_tests_properties(match_supervision PROPERTIES TIMEOUT 10)
endif()
