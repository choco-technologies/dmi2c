set(DMOD_MODULE_NAME dmi2c_port)
set(DMOD_AUTHOR_NAME "Patryk Kubiak")
set(DMOD_STACK_SIZE 2048)
set(DMOD_DMR_PATH ${CMAKE_SOURCE_DIR}/dmi2c_port.dmr)
if(NOT DMOD_CPU_FAMILY MATCHES "^stm32f[47]$")
    message(FATAL_ERROR "Supported I2C ports: stm32f4, stm32f7")
endif()
dmod_add_library(${DMOD_MODULE_NAME} ${DMOD_MODULE_VERSION}
    ${DMOD_CPU_FAMILY}/port.c
    stm32_common/stm32_common.c
    stm32_common/i2c_v1.c
    stm32_common/i2c_v2.c)
dmod_link_modules(${DMOD_MODULE_NAME} dmosi dmclk_port@1.1.1 dmdrvi@>=2.0)
target_include_directories(${DMOD_MODULE_NAME} BEFORE PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_include_directories(${DMOD_MODULE_NAME}_if INTERFACE ${CMAKE_SOURCE_DIR}/include)
