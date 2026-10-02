# ======================================================================
#               Loader integration tests
# ======================================================================
# The hardware regression is tools/i2ctest. This suite uses a test-only port
# module and the real loader, dmini, dmdrvi ABI and OS services.
if(NOT DMI2C_HOST_TESTS)
    return()
endif()

unset(DMOD_DMR_PATH)
set(DMOD_MODULE_NAME        test_dmi2c)
set(DMOD_MODULE_VERSION     ${PROJECT_VERSION})
set(DMOD_AUTHOR_NAME        "Patryk Kubiak")
set(DMOD_STACK_SIZE         4096)
set(DMOD_PRIORITY           0)

dmod_add_test(${DMOD_MODULE_NAME} ${DMOD_MODULE_VERSION}
    dmi2c_test.c
)

target_include_directories(${DMOD_MODULE_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

dmod_link_modules(${DMOD_MODULE_NAME} dmdrvi@>=2.0 dmini)

find_program(DMOD_LOADER_EXECUTABLE dmod_loader REQUIRED)
add_test(NAME loader_integration
    COMMAND ${CMAKE_COMMAND} -E env DMOD_DMF_DIR=${DMOD_DMF_DIR}
        ${DMOD_LOADER_EXECUTABLE} ${DMOD_DMF_DIR}/test_dmi2c.dmf
)
set_tests_properties(loader_integration PROPERTIES TIMEOUT 30)
