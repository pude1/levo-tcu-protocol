#include "board_i2c.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static i2c_master_bus_handle_t bus;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static SemaphoreHandle_t create_lock;

i2c_master_bus_handle_t board_i2c_bus(void) {
    portENTER_CRITICAL(&lock);
    if (!create_lock)
        create_lock = xSemaphoreCreateMutex();
    portEXIT_CRITICAL(&lock);
    configASSERT(create_lock);
    xSemaphoreTake(create_lock, portMAX_DELAY);
    if (!bus) {
        i2c_master_bus_config_t cfg = {.i2c_port = I2C_NUM_0,
                                       .sda_io_num = 18,
                                       .scl_io_num = 19,
                                       .clk_source = I2C_CLK_SRC_DEFAULT,
                                       .glitch_ignore_cnt = 7,
                                       .flags.enable_internal_pullup = true};
        ESP_ERROR_CHECK(i2c_new_master_bus(&cfg, &bus));
    }
    xSemaphoreGive(create_lock);
    return bus;
}
