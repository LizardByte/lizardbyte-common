set(TEST_BINARY test_lizardbyte_common)

add_executable(${TEST_BINARY} "${CMAKE_CURRENT_SOURCE_DIR}/cpp/test_env.cpp")

target_link_libraries(${TEST_BINARY} PRIVATE lizardbyte::common)

add_test(NAME ${TEST_BINARY} COMMAND ${TEST_BINARY})
