cmake_minimum_required(VERSION 2.8)

set(CMAKE_MODULE_PATH
  ${CMAKE_MODULE_PATH}
  "${CMAKE_SOURCE_DIR}/cmake/modules"
)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
find_package(LibClangTooling REQUIRED)
add_definitions(${LibClangTooling_DEFINITIONS})
include_directories(
  ${LibClangTooling_INCLUDE_DIRS}
)

add_executable(lcov LineCov.cpp)
target_link_libraries(lcov
  ${LibClangTooling_LIBRARIES}
)
