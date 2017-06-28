cmake_minimum_required(VERSION 2.8)

set(CMAKE_MODULE_PATH
  ${CMAKE_MODULE_PATH}
  "${CMAKE_SOURCE_DIR}/cmake/modules"
)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
include_directories(
  ../
)
add_library(br br.c)
add_executable(grep grep_bcov.c)
target_link_libraries(grep br)
