# crengine/tests/CMakeLists.txt
# Thai segmenter unit tests — gated on GTest availability.
find_package(GTest QUIET)
if(NOT GTest_FOUND)
    message(STATUS "GTest not found; skipping crengine tests")
    return()
endif()

add_executable(test_crengine_thai test_thai.cpp)
target_link_libraries(test_crengine_thai PRIVATE crengine GTest::gtest GTest::gtest_main)
target_include_directories(test_crengine_thai PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)
add_test(NAME crengine_thai COMMAND test_crengine_thai)
