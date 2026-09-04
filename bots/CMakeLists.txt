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

add_felt_bot(check_fold check_fold/check_fold.c)
add_felt_bot(check_call check_call/check_call.c)
add_felt_bot(always_all_in always_all_in/always_all_in.c)
add_felt_bot(seeded_random seeded_random/seeded_random.c)
add_felt_bot(nit_all_in nit_all_in/nit_all_in.c)
add_felt_bot(better_all_in better_all_in/better_all_in.c)
add_felt_bot(worse_all_in worse_all_in/worse_all_in.c)
add_felt_bot(solved_all_in solved_all_in/solved_all_in.c)

if(BUILD_TESTING)
  add_felt_bot(bad_abi tests/bad_abi.c)
  add_felt_bot(missing_act tests/missing_act.c)
  add_felt_bot(hanging tests/hanging.c)
  add_felt_bot(crashing tests/crashing.c)
endif()
