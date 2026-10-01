#define DMOD_ENABLE_REGISTRATION ON
#include "dmod_test.h"
#include "dmi2c.h"

static dmi2c_t g_handle = NULL;

void dmod_test_setup(void)
{
    g_handle = dmi2c_create();
}

void dmod_test_teardown(void)
{
    dmi2c_destroy(g_handle);
    g_handle = NULL;
}

DMOD_TEST_STEP(dmi2c_create)
{
    DMOD_TEST_EXPECT_NOT_NULL(g_handle);
}

DMOD_TEST_STEP(dmi2c_is_valid)
{
    DMOD_TEST_EXPECT_TRUE(dmi2c_is_valid(g_handle));
}

DMOD_TEST_STEP(dmi2c_destroy_null)
{
    /* Destroying NULL must not crash. */
    dmi2c_destroy(NULL);
}
