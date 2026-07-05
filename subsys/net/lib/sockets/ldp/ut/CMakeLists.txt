cmake_minimum_required(VERSION 3.14)

set(CMAKE_CXX_STANDARD 17)
project(LDP LANGUAGES CXX C)

# add dependencies
include(CPM.cmake)
include(coverage.cmake)

CPMAddPackage("gh:google/googletest@1.15.2")

set(SRC
    ut_bc.cpp
    ut_ldp_sm.cpp
    ut_ldp_utils.cpp
)
aux_source_directory(../src LDP_SRC)

add_executable(${PROJECT_NAME} ${SRC} ${LDP_SRC})
add_llvm_cov(${PROJECT_NAME})
target_link_libraries(${PROJECT_NAME} gmock_main gtest_main)
target_include_directories(${PROJECT_NAME} PUBLIC . ../inc)

add_executable(LDPNoLearner ut_ldp_nolearner.cpp ${LDP_SRC})
target_compile_definitions(LDPNoLearner PRIVATE CONFIG_CIF_ASYNC_COMEBACK_LEARNER=0)
target_link_libraries(LDPNoLearner gmock_main gtest_main)
target_include_directories(LDPNoLearner PUBLIC . ../inc)
