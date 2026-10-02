unset(DMOD_DMR_PATH)
set(DMOD_MODULE_NAME        test_dmi2c)
set(DMOD_MODULE_VERSION     ${PROJECT_VERSION})
set(DMOD_AUTHOR_NAME        "Patryk Kubiak")
set(DMOD_STACK_SIZE         1024)
set(DMOD_PRIORITY           0)

dmod_add_test(${DMOD_MODULE_NAME} ${DMOD_MODULE_VERSION}
    dmi2c_test.c
)

target_include_directories(${DMOD_MODULE_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

target_link_libraries(${DMOD_MODULE_NAME} dmi2c_if)
dmod_link_modules(${DMOD_MODULE_NAME} dmdrvi@>=2.0)

# This module runs on the selected MCU. Host CTest is in tests/host;
# never run a cross-compiled .dmf with the host dmod_loader.
