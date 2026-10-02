cmake_minimum_required(VERSION 3.18)
project(dmi2c_host_tests C)
enable_testing()
set(CMAKE_C_STANDARD 11)
add_executable(register_tests register_tests.c
    ../../src/port/stm32_common/stm32_common.c
    ../../src/port/stm32_common/i2c_v1.c
    ../../src/port/stm32_common/i2c_v2.c)
target_compile_definitions(register_tests PRIVATE I2C_TEST)
target_include_directories(register_tests PRIVATE stubs ../../include ../../src/port/stm32_common)
target_compile_options(register_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)
add_test(NAME register_protocols COMMAND register_tests)

add_executable(core_tests core_tests.c ../../src/dmi2c.c)
target_include_directories(core_tests PRIVATE stubs ../../include)
target_compile_options(core_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)
add_test(NAME driver_contract COMMAND core_tests)
