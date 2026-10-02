#define DMOD_ENABLE_REGISTRATION ON
#include "dmod_test.h"
#include "dmi2c.h"
void dmod_test_setup(void) {}
void dmod_test_teardown(void) {}
DMOD_TEST_STEP(config_validation)
{
    dmi2c_config_t c = {1, 0x50, 100000, 100};
    DMOD_TEST_EXPECT_TRUE(dmi2c_validate_config(&c));
    c.baudrate = 400000;
    DMOD_TEST_EXPECT_TRUE(dmi2c_validate_config(&c));
    c.timeout_ms = 0;
    DMOD_TEST_EXPECT_FALSE(dmi2c_validate_config(&c));
    DMOD_TEST_EXPECT_FALSE(dmi2c_validate_config(NULL));
}
DMOD_TEST_STEP(message_validation)
{
    uint8_t byte = 0;
    dmi2c_message_t m = {0x38, true, &byte, 1};
    dmi2c_transfer_t t = {&m, 1};
    DMOD_TEST_EXPECT_TRUE(dmi2c_validate_transfer(&t));
    m.size = 0;
    DMOD_TEST_EXPECT_FALSE(dmi2c_validate_transfer(&t));
    m.read = false;
    DMOD_TEST_EXPECT_TRUE(dmi2c_validate_transfer(&t));
    m.address = 0x80;
    DMOD_TEST_EXPECT_FALSE(dmi2c_validate_transfer(&t));
}
