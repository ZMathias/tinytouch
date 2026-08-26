#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "usb/usb_setup.h"

static const char *TAG = "tiny_touch";

void init_tusb() {
    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG();
    tusb_cfg.descriptor.full_speed_config = configuration_descriptor;
    tusb_cfg.descriptor.device = &device_descriptor;
    tusb_cfg.descriptor.string = string_desc_arr;
    tusb_cfg.descriptor.string_count = STRING_DESC_COUNT;

    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));
    ESP_LOGI(TAG, "Initialized FIDO2 HID");
}

void app_main(void)
{
    ESP_LOGI(TAG, "Initializing USB stack");
    init_tusb();

    // NOLINTNEXTLINE
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}