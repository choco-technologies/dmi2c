#define DMOD_ENABLE_REGISTRATION ON
#define ENABLE_DIF_REGISTRATIONS ON
#include "dmod_test.h"
#include "dmdrvi.h"
#include "dmini.h"
#include "dmi2c.h"
#include <errno.h>
#include <string.h>

/**
 * @brief Real driver DIF resolved through dmod_loader, as dmdevfs resolves it.
 */
static struct
{
    dmod_dmdrvi_create_t create;
    dmod_dmdrvi_free_t free;
    dmod_dmdrvi_open_t open;
    dmod_dmdrvi_close_t close;
    dmod_dmdrvi_read_t read;
    dmod_dmdrvi_write_t write;
    dmod_dmdrvi_ioctl_t ioctl;
} driver;

static dmini_context_t config;
static dmdrvi_context_t context;
static dmdrvi_dev_num_t device;
static void *handle;
static bool ready;

/**
 * @brief Load the production core and obtain its actual DIF exports.
 */
static bool load_driver(void)
{
    if (Dmod_LoadModuleByName("dmi2c") == NULL || !Dmod_EnableModule("dmi2c", false, NULL))
    {
        return false;
    }
    Dmod_Context_t *module = Dmod_GetModuleContext("dmi2c");
    driver.create = Dmod_GetDifFunction(module, dmod_dmdrvi_create_sig);
    driver.free = Dmod_GetDifFunction(module, dmod_dmdrvi_free_sig);
    driver.open = Dmod_GetDifFunction(module, dmod_dmdrvi_open_sig);
    driver.close = Dmod_GetDifFunction(module, dmod_dmdrvi_close_sig);
    driver.read = Dmod_GetDifFunction(module, dmod_dmdrvi_read_sig);
    driver.write = Dmod_GetDifFunction(module, dmod_dmdrvi_write_sig);
    driver.ioctl = Dmod_GetDifFunction(module, dmod_dmdrvi_ioctl_sig);
    return driver.create && driver.free && driver.open && driver.close && driver.read &&
           driver.write && driver.ioctl;
}

/**
 * @brief Create the same restricted configuration view supplied by dmdevfs.
 */
void dmod_test_setup(void)
{
    ready = load_driver();
    config = dmini_create_with_token(0x1234);
    context = NULL;
    handle = NULL;
    if (!ready || config == NULL)
    {
        return;
    }
    dmini_parse_string(config,
                       "[unrelated]\ninstance=99\naddress=1\n"
                       "[touch_i2c]\ninstance=1\naddress=56\nbaudrate=100000\ntimeout_ms=20\n");
    dmini_set_active_section(config, "touch_i2c", 0x1234);
    context = driver.create(config, &device);
    if (context != NULL)
    {
        handle = driver.open(context, DMDRVI_O_RDWR, &device);
    }
}

/**
 * @brief Close handles before releasing the bus and the real dmini context.
 */
void dmod_test_teardown(void)
{
    if (handle != NULL)
    {
        driver.close(context, handle);
    }
    if (context != NULL)
    {
        driver.free(context);
    }
    if (config != NULL)
    {
        dmini_destroy(config);
    }
}

/**
 * @brief Verify active-section isolation, device naming and configuration.
 */
DMOD_TEST_STEP(active_section_configuration)
{
    DMOD_TEST_EXPECT_TRUE(ready);
    DMOD_TEST_EXPECT_NOT_NULL(context);
    DMOD_TEST_EXPECT_NOT_NULL(handle);
    if (handle == NULL)
    {
        return;
    }
    dmi2c_config_t value;
    DMOD_TEST_EXPECT_EQ(driver.ioctl(context, handle, dmi2c_ioctl_cmd_get_config, &value), 0);
    DMOD_TEST_EXPECT_EQ(value.instance, 1);
    DMOD_TEST_EXPECT_EQ(value.address, 56);
    DMOD_TEST_EXPECT_TRUE(strcmp(device.alt_name, "touch_i2c") == 0);
}

/**
 * @brief Verify independent handle addresses and rejection of foreign ioctls.
 */
DMOD_TEST_STEP(handle_addresses_and_ioctl_contract)
{
    DMOD_TEST_EXPECT_NOT_NULL(handle);
    if (handle == NULL)
    {
        return;
    }
    void *second = driver.open(context, DMDRVI_O_RDWR, &device);
    DMOD_TEST_EXPECT_NOT_NULL(second);
    if (second == NULL)
    {
        return;
    }
    uint16_t address = 0x39;
    DMOD_TEST_EXPECT_EQ(driver.ioctl(context, second, dmi2c_ioctl_cmd_set_address, &address), 0);
    DMOD_TEST_EXPECT_EQ(driver.ioctl(context, handle, dmi2c_ioctl_cmd_get_address, &address), 0);
    DMOD_TEST_EXPECT_EQ(address, 0x38);
    DMOD_TEST_EXPECT_EQ(driver.ioctl(context, handle, DMDRVI_IOCTL_BLOCK_GET_INFO, NULL), -ENOTTY);
    DMOD_TEST_EXPECT_EQ(driver.ioctl(context, handle, dmi2c_ioctl_cmd_get_address, NULL), -EINVAL);
    address = 0x80;
    DMOD_TEST_EXPECT_EQ(driver.ioctl(context, handle, dmi2c_ioctl_cmd_set_address, &address),
                        -EINVAL);
    driver.close(context, second);
}

/**
 * @brief Check repeated START and the externally visible transfer result.
 */
DMOD_TEST_STEP(register_read_vector)
{
    DMOD_TEST_EXPECT_NOT_NULL(handle);
    if (handle == NULL)
    {
        return;
    }
    uint8_t reg = 0xa8, value = 0;
    dmi2c_message_t messages[] = {{0x38, false, &reg, 1}, {0x38, true, &value, 1}};
    dmi2c_transfer_t transfer = {messages, 2};
    DMOD_TEST_EXPECT_EQ(driver.ioctl(context, handle, dmi2c_ioctl_cmd_transfer, &transfer), 0);
    DMOD_TEST_EXPECT_EQ(value, 0x51);
    messages[1].size = 0;
    DMOD_TEST_EXPECT_EQ(driver.ioctl(context, handle, dmi2c_ioctl_cmd_transfer, &transfer),
                        -EINVAL);
    DMOD_TEST_EXPECT_EQ(driver.read(context, handle, NULL, 0, 0), 0);
    DMOD_TEST_EXPECT_EQ(driver.write(context, handle, NULL, 0, 0), 0);
    DMOD_TEST_EXPECT_EQ(driver.read(context, handle, &value, 1, -1), -EINVAL);
}

/**
 * @brief Confirm an existing context survives a duplicate claim and error.
 */
DMOD_TEST_STEP(duplicate_claim_and_nack_recovery)
{
    DMOD_TEST_EXPECT_NOT_NULL(handle);
    if (handle == NULL)
    {
        return;
    }
    dmdrvi_dev_num_t other;
    dmdrvi_context_t duplicate = driver.create(config, &other);
    DMOD_TEST_EXPECT_TRUE(duplicate == NULL);
    if (duplicate != NULL)
    {
        driver.free(duplicate);
    }
    uint16_t address = 0x77;
    DMOD_TEST_EXPECT_EQ(driver.ioctl(context, handle, dmi2c_ioctl_cmd_probe, &address), -ENXIO);
    address = 0x38;
    DMOD_TEST_EXPECT_EQ(driver.ioctl(context, handle, dmi2c_ioctl_cmd_probe, &address), 0);
}

/**
 * @brief Exercise cancellation, errno propagation and reuse after a deadline.
 */
DMOD_TEST_STEP(timeout_and_error_recovery)
{
    DMOD_TEST_EXPECT_NOT_NULL(handle);
    if (handle == NULL)
    {
        return;
    }
    uint16_t address = 0x70;
    Dmod_Timestamp_t started = Dmod_GetUptime();
    DMOD_TEST_EXPECT_EQ(driver.ioctl(context, handle, dmi2c_ioctl_cmd_probe, &address), -ETIMEDOUT);
    DMOD_TEST_EXPECT_TRUE(Dmod_GetUptime() - started >= 20);
    address = 0x71;
    DMOD_TEST_EXPECT_EQ(driver.ioctl(context, handle, dmi2c_ioctl_cmd_probe, &address), -EAGAIN);
    address = 0x72;
    DMOD_TEST_EXPECT_EQ(driver.ioctl(context, handle, dmi2c_ioctl_cmd_probe, &address), -EIO);
    address = 0x38;
    DMOD_TEST_EXPECT_EQ(driver.ioctl(context, handle, dmi2c_ioctl_cmd_probe, &address), 0);
}

/**
 * @brief Verify supported numeric ranges using the real INI parser.
 */
DMOD_TEST_STEP(configuration_ranges)
{
    DMOD_TEST_EXPECT_TRUE(ready);
    if (!ready || config == NULL)
    {
        return;
    }
    if (handle != NULL)
    {
        driver.close(context, handle);
        handle = NULL;
    }
    if (context != NULL)
    {
        driver.free(context);
        context = NULL;
    }
    dmini_set_int(config, NULL, "instance", 0);
    DMOD_TEST_EXPECT_TRUE(driver.create(config, &device) == NULL);
    dmini_set_int(config, NULL, "instance", 1);
    dmini_set_int(config, NULL, "timeout_ms", -1);
    DMOD_TEST_EXPECT_TRUE(driver.create(config, &device) == NULL);
    dmini_set_int(config, NULL, "timeout_ms", 20);
    dmini_set_int(config, NULL, "baudrate", 400000);
    context = driver.create(config, &device);
    DMOD_TEST_EXPECT_NOT_NULL(context);
}

/**
 * @brief Wake from a real OS semaphore after completion on another thread.
 */
DMOD_TEST_STEP(deferred_completion)
{
    DMOD_TEST_EXPECT_NOT_NULL(handle);
    if (handle == NULL)
    {
        return;
    }
    uint16_t address = 0x69;
    Dmod_Timestamp_t started = Dmod_GetUptime();
    DMOD_TEST_EXPECT_EQ(driver.ioctl(context, handle, dmi2c_ioctl_cmd_probe, &address), 0);
    DMOD_TEST_EXPECT_TRUE(Dmod_GetUptime() - started >= 5);
    address = 0x38;
    DMOD_TEST_EXPECT_EQ(driver.ioctl(context, handle, dmi2c_ioctl_cmd_probe, &address), 0);
}

/**
 * @brief Apply one deadline to the vector instead of restarting it per message.
 */
DMOD_TEST_STEP(whole_vector_deadline)
{
    DMOD_TEST_EXPECT_NOT_NULL(handle);
    if (handle == NULL)
    {
        return;
    }
    dmi2c_message_t messages[10];
    for (size_t i = 0; i < 10; ++i)
    {
        messages[i] = (dmi2c_message_t){0x69, false, NULL, 0};
    }
    dmi2c_transfer_t transfer = {messages, 10};
    DMOD_TEST_EXPECT_EQ(driver.ioctl(context, handle, dmi2c_ioctl_cmd_transfer, &transfer),
                       -ETIMEDOUT);
    uint16_t address = 0x38;
    DMOD_TEST_EXPECT_EQ(driver.ioctl(context, handle, dmi2c_ioctl_cmd_probe, &address), 0);
}
