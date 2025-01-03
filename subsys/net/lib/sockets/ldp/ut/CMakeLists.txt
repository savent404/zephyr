cmake_minimum_required(VERSION 3.14)

set(CMAKE_CXX_STANDARD 17)
project(LDP LANGUAGES CXX C)

# add dependencies
include(CPM.cmake)
include(coverage.cmake)

CPMAddPackage("gh:google/googletest@1.15.2")

aux_source_directory(. SRC)
add_executable(${PROJECT_NAME} ${SRC})
add_llvm_cov(${PROJECT_NAME})
target_link_libraries(${PROJECT_NAME} gmock_main gtest_main)
target_include_directories(${PROJECT_NAME} PUBLIC . ../inc)
