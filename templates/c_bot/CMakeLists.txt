# Standalone build for one Felt bot. Point FELT_INCLUDE at the harness headers.
cmake_minimum_required(VERSION 3.20)
project(my_bot C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(FELT_INCLUDE "" CACHE PATH "Path to Felt's harness/include directory")
if(NOT FELT_INCLUDE)
  message(FATAL_ERROR "Set -DFELT_INCLUDE=/path/to/felt/harness/include")
endif()

add_library(my_bot SHARED my_bot.c)
target_include_directories(my_bot PRIVATE "${FELT_INCLUDE}")
target_compile_options(my_bot PRIVATE -O2 -Wall -Wextra -fvisibility=hidden)
set_target_properties(my_bot PROPERTIES PREFIX "" SUFFIX ".dylib")
