cmake_minimum_required(VERSION 3.14)
project(c-behavior-tree C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror -pedantic")

# Core library
add_library(bt STATIC src/bt.c)
target_include_directories(bt PUBLIC include)

# Tests
enable_testing()
add_executable(bt_test tests/test_c-behavior-tree.c src/bt.c)
target_include_directories(bt_test PRIVATE include)
add_test(NAME bt_test COMMAND bt_test)

# Examples
add_executable(bt_example_posix examples/bt_example_posix.c src/bt.c)
target_include_directories(bt_example_posix PRIVATE include)

add_executable(simple_robot examples/simple_robot.c src/bt.c)
target_include_directories(simple_robot PRIVATE include)

add_executable(state_machine examples/state_machine.c src/bt.c)
target_include_directories(state_machine PRIVATE include)

# Code quality
find_program(CLANG_FORMAT clang-format)
if(CLANG_FORMAT)
    add_custom_target(format
        COMMAND ${CLANG_FORMAT} -i include/*.h src/*.c tests/*.c examples/*.c
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )
endif()

find_program(CPPCHECK cppcheck)
if(CPPCHECK)
    add_custom_target(cppcheck
        COMMAND ${CPPCHECK} --enable=all --suppress=missingIncludeSystem
                include/ src/ tests/ examples/
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )
endif()
