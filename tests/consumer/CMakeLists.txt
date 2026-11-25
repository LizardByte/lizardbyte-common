cmake_minimum_required(VERSION 3.24)
project(logging_consumer LANGUAGES CXX)

find_package(lizardbyte-common CONFIG REQUIRED)
add_executable(logging_consumer main.cpp)
target_link_libraries(logging_consumer PRIVATE lizardbyte::common)
