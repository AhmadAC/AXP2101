#include <stdio.h>
#include <cstring>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_err.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "sdkconfig.h"

#define TAG "main"

// Definitive I2C Pins for Waveshare ESP32-S3-Touch-AMOLED-2.06
#define I2C_MASTER_NUM (i2c_port_num_t) 0
#define I2C_MASTER_FREQ_HZ 100000  // 100kHz for stability on the shared bus
#define I2C_MASTER_SDA_IO (gpio_num_t) 15
#define I2C_MASTER_SCL_IO (gpio_num_t) 14
#define I2C_MASTER_TIMEOUT_MS 1000

static i2c_master_bus_handle_t i2c_bus_handle = NULL;
static i2c_master_dev_handle_t pmu_dev_handle = NULL;

// Function declarations
extern esp_err_t pmu_init();
extern void pmu_isr_handler();

// I2C init with new API
esp_err_t i2c_init() {
    i2c_master_bus_config_t bus_config = {};
    bus_config.i2c_port = I2C_MASTER_NUM;
    bus_config.sda_io_num = I2C_MASTER_SDA_IO;
    bus_config.scl_io_num = I2C_MASTER_SCL_IO;
    bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_config.glitch_ignore_cnt = 7;
    bus_config.intr_priority = 0;
    bus_config.trans_queue_depth = 0;
    bus_config.flags.enable_internal_pullup = 1;

    esp_err_t err = i2c_new_master_bus(&bus_config, &i2c_bus_handle);
    if (err != ESP_OK) return err;

    i2c_device_config_t dev_config = {};
    dev_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_config.device_address = 0x34; // AXP2101 Address
    dev_config.scl_speed_hz = I2C_MASTER_FREQ_HZ;

    return i2c_master_bus_add_device(i2c_bus_handle, &dev_config, &pmu_dev_handle);
}

// PMU read function using new API
int pmu_register_read(uint8_t devAddr, uint8_t regAddr, uint8_t *data, uint8_t len) {
    esp_err_t ret = i2c_master_transmit_receive(pmu_dev_handle, &regAddr, 1, data, len, I2C_MASTER_TIMEOUT_MS);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "PMU READ FAILED! Reg: 0x%02X, Error: %s", regAddr, esp_err_to_name(ret));
        return -1;
    }
    return 0;
}

// PMU write function using new API
int pmu_register_write_byte(uint8_t devAddr, uint8_t regAddr, uint8_t *data, uint8_t len) {
    uint8_t *buffer = (uint8_t *)malloc(len + 1);
    if (!buffer) return -1;
    buffer[0] = regAddr;
    memcpy(&buffer[1], data, len);

    esp_err_t ret = i2c_master_transmit(pmu_dev_handle, buffer, len + 1, I2C_MASTER_TIMEOUT_MS);
    free(buffer);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "PMU WRITE FAILED! Reg: 0x%02X, Error: %s", regAddr, esp_err_to_name(ret));
        return -1;
    }
    return 0;
}

// PMU event task
static void pmu_hander_task(void *args) {
    while (1) {
        pmu_isr_handler();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

extern "C" void app_main(void) {
    esp_err_t err = i2c_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C initialization failed: %s", esp_err_to_name(err));
        return;
    }
    
    ESP_LOGI(TAG, "I2C initialized successfully on SDA:%d SCL:%d", I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO);

    err = pmu_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "AXP2101 PMU initialization failed! Halting.");
        // Halt here to prevent bootloop so you can read the log output
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    xTaskCreate(pmu_hander_task, "App/pwr", 4 * 1024, NULL, 10, NULL);
}