# ======================================================================
#               dmi2c_port Module Configuration
# ======================================================================

# Name of the module
set(DMOD_MODULE_NAME        dmi2c_port)

# Version is already set above and used in project()

# Author (should be string)
set(DMOD_AUTHOR_NAME        "Patryk Kubiak")

# Stack size for the module (should be integer)
set(DMOD_STACK_SIZE         1024)

# Path to the DMR file for this module
set(DMOD_DMR_PATH           ${CMAKE_SOURCE_DIR}/dmi2c_port.dmr)

# Hardware port selection; the virtual peripheral is only built for loader tests.
if(DMI2C_HOST_TESTS)
    set(PORT_SOURCES ${CMAKE_SOURCE_DIR}/tests/virtual_port/port.c)
else()
    set(PORT_SOURCES
        ${DMOD_CPU_FAMILY}/port.c
        stm32_common/stm32_common.c
    )
    if(DMOD_CPU_FAMILY STREQUAL "stm32f4")
        list(APPEND PORT_SOURCES stm32_common/i2c_v1.c)
        set(I2C_IP_VERSION 1)
    elseif(DMOD_CPU_FAMILY STREQUAL "stm32f7")
        list(APPEND PORT_SOURCES stm32_common/i2c_v2.c)
        set(I2C_IP_VERSION 2)
    else()
        message(FATAL_ERROR "Unsupported hardware family: ${DMOD_CPU_FAMILY}")
    endif()
endif()

#
#   dmod_add_library - create a library module
#   it has the same signature as add_library
#   and can be used in the same way after the creation
#   (for example, to link libraries)
#
dmod_add_library(${DMOD_MODULE_NAME} ${DMOD_MODULE_VERSION}
    # List of source files - can include C and C++ files
    ${PORT_SOURCES}
)

# dmosi supplies the RTOS-safe IRQ priority; synchronization belongs to the core.
dmod_link_modules(${DMOD_MODULE_NAME} dmdrvi@>=2.0 dmosi)
if(NOT DMI2C_HOST_TESTS)
    target_compile_definitions(${DMOD_MODULE_NAME} PRIVATE DMI2C_IP_VERSION=${I2C_IP_VERSION})
    dmod_link_modules(${DMOD_MODULE_NAME} dmclk_port@1.1.1)
endif()

target_include_directories(${DMOD_MODULE_NAME} BEFORE PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_include_directories(${DMOD_MODULE_NAME}_if INTERFACE
    ${CMAKE_SOURCE_DIR}/include
)
