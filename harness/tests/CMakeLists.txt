add_executable(felt_api_layout_test api_layout_test.cpp)
target_link_libraries(felt_api_layout_test PRIVATE felt_bot_runner)
felt_configure_target(felt_api_layout_test)

add_executable(felt_native_bot_runner_test native_bot_runner_test.cpp)
target_link_libraries(felt_native_bot_runner_test PRIVATE felt_bot_runner)
felt_configure_target(felt_native_bot_runner_test)

add_test(NAME api_layout COMMAND felt_api_layout_test)
add_test(
  NAME native_bot_runner
  COMMAND
    felt_native_bot_runner_test
    $<TARGET_FILE:always_fold>
    $<TARGET_FILE:check_call>
    $<TARGET_FILE:always_all_in>
    $<TARGET_FILE:seeded_random>
    $<TARGET_FILE:bad_abi>
    $<TARGET_FILE:missing_act>
)
add_test(
  NAME run_match_probe
  COMMAND run_match $<TARGET_FILE:always_fold> $<TARGET_FILE:check_call>
)
