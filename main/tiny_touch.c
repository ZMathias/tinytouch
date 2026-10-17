#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "driver/gpio.h"
#include "ctaphid/ctaphid.h"
#include "esp_log.h"

static const char *TAG = "main_task";

void ctap_task_loop(void *pvParameters) {
    struct CTAP_REPORT_T report;
    while (1) {
        if (xQueueReceive(reportQueue, &report, portMAX_DELAY) == pdPASS) {
            parse_packet(&report);
            memset(&report, 0, sizeof(struct CTAP_REPORT_T));
        }
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Initializing CTAPHID USB Endpoint");
    // set up message queue then initialize usb stack and then ctap parser task
    reportQueue = xQueueCreate(32, sizeof(struct CTAP_REPORT_T));
    ctaphid_init();

    // create CTAP parser task
    TaskHandle_t ctapTaskHandle;
    if (xTaskCreate(ctap_task_loop, "ctap_parse_task", 4096, nullptr, 5, &ctapTaskHandle) != pdPASS) {
        ESP_LOGE(TAG, "Fatal error, couldn't create ctap_parse_task");
        abort();
    }

    // NOLINTNEXTLINE
    while (1) {
        vTaskDelay(portMAX_DELAY);
    }
}