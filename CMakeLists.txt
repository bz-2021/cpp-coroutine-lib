cmake_minimum_required(VERSION 3.16)
project(CoroutineLib)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

include_directories(include)

file(GLOB SOURCES "src/*.cpp")
file(GLOB TEST_SOURCES "tests/*.cpp")

foreach(TEST_FILE ${TEST_SOURCES})
    get_filename_component(TEST_NAME ${TEST_FILE} NAME_WE)
    add_executable(${TEST_NAME} ${SOURCES} ${TEST_FILE})
    target_link_libraries(${TEST_NAME} Threads::Threads)
endforeach()

find_package(Threads REQUIRED)
