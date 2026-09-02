#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "driver/gpio.h"
#include "ctaphid/ctaphid.h"
#include "esp_log.h"

static const char *TAG = "main_task";

void app_main(void)
{
    ESP_LOGI(TAG, "Initializing CTAPHID USB Endpoint");
    ctaphid_init();

    // NOLINTNEXTLINE
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}