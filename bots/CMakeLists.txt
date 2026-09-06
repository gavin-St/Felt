function(add_felt_bot target source)
  add_library(${target} SHARED ${source})
  target_include_directories(
    ${target}
    PRIVATE
      ${PROJECT_SOURCE_DIR}/harness/include
  )
  set_target_properties(
    ${target}
    PROPERTIES
      PREFIX ""
      LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bots"
  )
  felt_configure_target(${target})
endfunction()

function(add_felt_bot_with_kit target source)
  add_felt_bot(${target} ${source})
  target_link_libraries(${target} PRIVATE felt_botkit)
endfunction()

add_felt_bot(check_fold check_fold/check_fold.c)
add_felt_bot(check_call check_call/check_call.c)
add_felt_bot(always_all_in always_all_in/always_all_in.c)
add_felt_bot(seeded_random seeded_random/seeded_random.c)
add_felt_bot(nit_all_in nit_all_in/nit_all_in.c)
add_felt_bot(better_all_in better_all_in/better_all_in.c)
add_felt_bot(worse_all_in worse_all_in/worse_all_in.c)
add_felt_bot(solved_all_in solved_all_in/solved_all_in.c)

add_felt_bot_with_kit(
  slp_fold
  slp_fold/slp_fold.c
)
target_sources(slp_fold PRIVATE slp_common.c)

add_felt_bot_with_kit(
  slp_bluff
  slp_bluff/slp_bluff.c
)
target_sources(slp_bluff PRIVATE slp_common.c)

add_felt_bot_with_kit(
  slp_balance
  slp_balance/slp_balance.c
)
target_sources(slp_balance PRIVATE slp_common.c)

add_felt_bot_with_kit(
  slp_exploit_fold
  slp_exploit_fold/slp_exploit_fold.c
)
target_sources(slp_exploit_fold PRIVATE slp_common.c)

add_felt_bot_with_kit(
  slp_exploit_solved
  slp_exploit_solved/slp_exploit_solved.c
)
target_sources(slp_exploit_solved PRIVATE slp_common.c)

add_felt_bot_with_kit(
  nitty_nancy
  nitty_nancy/nitty_nancy.c
)
target_sources(nitty_nancy PRIVATE slp_common.c archetype_common.c)

add_felt_bot_with_kit(
  calling_station
  calling_station/calling_station.c
)
target_sources(calling_station PRIVATE slp_common.c archetype_common.c)

add_felt_bot_with_kit(
  passive_patty
  passive_patty/passive_patty.c
)
target_sources(passive_patty PRIVATE slp_common.c archetype_common.c)

add_felt_bot_with_kit(
  chasing_charlie
  chasing_charlie/chasing_charlie.c
)
target_sources(chasing_charlie PRIVATE slp_common.c archetype_common.c)

add_felt_bot_with_kit(
  semi_bluff_sarah
  semi_bluff_sarah/semi_bluff_sarah.c
)
target_sources(semi_bluff_sarah PRIVATE slp_common.c archetype_common.c)

add_felt_bot_with_kit(
  trapping_thomas
  trapping_thomas/trapping_thomas.c
)
target_sources(trapping_thomas PRIVATE slp_common.c archetype_common.c)

add_felt_bot_with_kit(
  triple_barrel_travis
  triple_barrel_travis/triple_barrel_travis.c
)
target_sources(triple_barrel_travis PRIVATE slp_common.c archetype_common.c)

add_felt_bot_with_kit(
  one_and_done
  one_and_done/one_and_done.c
)
target_sources(one_and_done PRIVATE slp_common.c archetype_common.c)

add_felt_bot_with_kit(
  scared_sam
  scared_sam/scared_sam.c
)
target_sources(scared_sam PRIVATE slp_common.c archetype_common.c)

add_felt_bot_with_kit(
  tilted_terry
  tilted_terry/tilted_terry.c
)
target_sources(tilted_terry PRIVATE slp_common.c archetype_common.c)

add_felt_bot_with_kit(
  min_raise_miranda
  min_raise_miranda/min_raise_miranda.c
)
target_sources(min_raise_miranda PRIVATE slp_common.c archetype_common.c)

add_felt_bot_with_kit(
  overbet_oliver
  overbet_oliver/overbet_oliver.c
)
target_sources(overbet_oliver PRIVATE slp_common.c archetype_common.c)

add_felt_bot_with_kit(
  aggressive_andy
  aggressive_andy/aggressive_andy.c
)
target_sources(aggressive_andy PRIVATE slp_common.c archetype_common.c)

if(BUILD_TESTING)
  add_felt_bot(bad_abi tests/bad_abi.c)
  add_felt_bot(missing_act tests/missing_act.c)
  add_felt_bot(hanging tests/hanging.c)
  add_felt_bot(crashing tests/crashing.c)
endif()
