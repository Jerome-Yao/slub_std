cmake_minimum_required(VERSION 3.10)
set(CMAKE_CXX_STANDARD 23)
project(slub)

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 开启优化选项
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -DNDEBUG")

add_executable(main tests.cpp slub.cpp)
target_include_directories(main PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(bench bench.cpp slub.cpp)
target_include_directories(bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_custom_target(run
    COMMAND $<TARGET_FILE:main>
      DEPENDS main
      WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
      USES_TERMINAL
)

add_custom_target(benchmark
    COMMAND $<TARGET_FILE:bench>
      DEPENDS bench
      WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
      USES_TERMINAL
)

ADD_CUSTOM_TARGET(TARGET main POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_CURRENT_BINARY_DIR}/compile_commands.json
    ${CMAKE_CURRENT_SOURCE_DIR}/compile_commands.json
)
