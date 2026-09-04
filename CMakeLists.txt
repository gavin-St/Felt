cmake_minimum_required(VERSION 3.24)

project(
  Felt
  VERSION 0.1.0
  LANGUAGES C CXX
)

if(NOT APPLE)
  message(FATAL_ERROR "Felt v1 currently supports macOS only")
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")

option(FELT_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(FELT_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)

function(felt_configure_target target)
  target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)

  if(FELT_ENABLE_ASAN)
    target_compile_options(${target} PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(${target} PRIVATE -fsanitize=address)
  endif()

  if(FELT_ENABLE_UBSAN)
    target_compile_options(${target} PRIVATE -fsanitize=undefined -fno-omit-frame-pointer)
    target_link_options(${target} PRIVATE -fsanitize=undefined)
  endif()
endfunction()

include(CTest)

add_subdirectory(harness)
add_subdirectory(bots)

if(BUILD_TESTING)
  add_subdirectory(harness/tests)
endif()
