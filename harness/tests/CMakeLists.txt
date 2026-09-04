add_executable(felt_api_layout_test api_layout_test.cpp)
target_link_libraries(felt_api_layout_test PRIVATE felt_bot_runner)
felt_configure_target(felt_api_layout_test)

add_executable(felt_native_bot_runner_test native_bot_runner_test.cpp)
target_link_libraries(felt_native_bot_runner_test PRIVATE felt_bot_runner)
felt_configure_target(felt_native_bot_runner_test)

add_executable(felt_card_random_test card_random_test.cpp)
target_link_libraries(felt_card_random_test PRIVATE felt_core)
felt_configure_target(felt_card_random_test)

add_executable(felt_evaluator_test evaluator_test.cpp)
target_link_libraries(felt_evaluator_test PRIVATE felt_core)
felt_configure_target(felt_evaluator_test)

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
    $<TARGET_FILE:seeded_random>
    $<TARGET_FILE:bad_abi>
    $<TARGET_FILE:missing_act>
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
add_test(NAME card_random COMMAND felt_card_random_test)
add_test(NAME evaluator COMMAND felt_evaluator_test)
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
