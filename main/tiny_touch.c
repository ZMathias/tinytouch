#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "r503.h"
#include "r503_defs.h"
#include "r503_errors.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "usb/usb_descriptors.h"
#include <fcntl.h>

constexpr int FPRINT_TXD_PIN = GPIO_NUM_17;
constexpr int FPRINT_RXD_PIN = GPIO_NUM_18;
constexpr int FPRINT_UART_PORT = UART_NUM_1;
constexpr int FPRINT_TOUCH_IRQ = GPIO_NUM_7;

static TaskHandle_t main_task_handle = NULL;

static const char *TAG = "tiny_touch";

static void IRAM_ATTR touch_isr_handler(void* arg) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    // Notify the main task that a touch occurred
    if (main_task_handle != NULL) {
        vTaskNotifyGiveFromISR(main_task_handle, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}

void init_tusb() {
    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG();
    tusb_cfg.descriptor.full_speed_config = configuration_descriptor;
    tusb_cfg.descriptor.device = &device_descriptor;
    tusb_cfg.descriptor.string = string_desc_arr;
    tusb_cfg.descriptor.string_count = STRING_DESC_COUNT;

    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));
    ESP_LOGI(TAG, "Initialized FIDO2 HID");
}

esp_err_t capture_and_convert(r503_t *sensor, uint8_t buffer_id) {
    esp_err_t err;

    // Block until a finger is pressed and successfully read
    do {
        err = r503_get_image(sensor);
        if (err != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(50)); // Yield to FreeRTOS scheduler
        }
    } while (err != ESP_OK);

    ESP_LOGI(TAG, "Image captured!");

    // Convert the image to a feature template in the specified buffer (1 or 2)
    err = r503_gen_char(sensor, buffer_id);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to convert image: %s", r503_err_to_name(err));
    }
    return err;
}

// --- 2. Manual Enrollment Process ---
void enroll_finger_manual(r503_t *sensor, uint16_t id) {
    ESP_LOGI(TAG, "\n--- ENROLLING ID %u ---", id);
    ESP_LOGI(TAG, "Place your finger on the sensor...");

    if (capture_and_convert(sensor, 1) != ESP_OK) return;

    ESP_LOGI(TAG, "Remove finger...");
    vTaskDelay(pdMS_TO_TICKS(2000)); // Give the user 2 seconds to remove their finger

    // Wait until the finger is actually removed (image grab fails)
    while (r503_get_image(sensor) == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    ESP_LOGI(TAG, "Place the SAME finger again...");
    if (capture_and_convert(sensor, 2) != ESP_OK) return;

    ESP_LOGI(TAG, "Merging templates...");
    if (r503_reg_model(sensor) != ESP_OK) {
        ESP_LOGE(TAG, "Prints did not match. Please try again.");
        return;
    }

    // Store the merged model from buffer 1 into flash memory at the specified ID
    if (r503_store(sensor, 1, id) == ESP_OK) {
        ESP_LOGI(TAG, "SUCCESS! Fingerprint saved to sensor memory at ID %u", id);
    } else {
        ESP_LOGE(TAG, "Failed to write to sensor memory.");
    }
}

// --- 3. Manual Authentication Loop ---
void authenticate_finger(r503_t *sensor) {
    // Try to grab an image. If no finger is present, return immediately.
    if (r503_get_image(sensor) == ESP_OK) {

        // Convert image to features in buffer 1
        if (r503_gen_char(sensor, 1) == ESP_OK) {

            r503_search_result_t result = {0};

            // Search buffer 1 against the whole database (Page 1 to 150)
            esp_err_t err = r503_search(sensor, 1, 1, 150, &result);

            if (err == ESP_OK) {
                ESP_LOGI(TAG, "\n*** MATCH FOUND! *** ID: %u | Score: %u", result.match_id, result.match_score);

                // ---------------------------------------------------------
                // TODO: TRIGGER YOUR FIDO2 USB HID ASSERTION LOGIC HERE
                // ---------------------------------------------------------

            } else {
                ESP_LOGW(TAG, "\nNo match found (Unknown Finger or Search Error).");
            }
        }

        // Debounce: Wait a second so it doesn't spam the log while the finger is still resting there
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}


void app_main(void)
{
    // 1. Initialize Sensor UART
    r503_t sensor = {0};
    r503_config_t cfg = {
        .uart_num = FPRINT_UART_PORT,
        .tx_pin = FPRINT_TXD_PIN,
        .rx_pin = FPRINT_RXD_PIN,
        .baud_rate = 115200,
        .address = R503_DEFAULT_ADDRESS,
        .password = R503_DEFAULT_PASSWORD,
        .rx_timeout_ms = 1000,
        .uart_buffer_size = 512
    };

    ESP_ERROR_CHECK(r503_init(&sensor, &cfg));

    // Wait for boot (ignoring timeout for clone compatibility)
    esp_err_t err = r503_wait_ready(&sensor, 500);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "R503 ready byte received.");
    }

    ESP_ERROR_CHECK(r503_verify_password(&sensor));

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << FPRINT_TOUCH_IRQ),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE, // Pull down to prevent floating states
        .intr_type = GPIO_INTR_POSEDGE        // Trigger on rising edge (0V to 3.3V)
    };
    gpio_config(&io_conf);

    // Install the global ISR service and attach our specific handler
    gpio_install_isr_service(0);
    gpio_isr_handler_add(FPRINT_TOUCH_IRQ, touch_isr_handler, NULL);

    // Save the current task handle so the ISR knows what to wake up
    main_task_handle = xTaskGetCurrentTaskHandle();

    // 2. Configure stdin (serial input) to be non-blocking
    int flags = fcntl(fileno(stdin), F_GETFL);
    fcntl(fileno(stdin), F_SETFL, flags | O_NONBLOCK);

    r503_aura_led_config(&sensor, R503_LED_ALWAYS_ON, 5, R503_LED_BLUE, 0);

    ESP_LOGI(TAG, "\n==============================================");
    ESP_LOGI(TAG, "INSTRUCTIONS:");
    ESP_LOGI(TAG, "1. Type 'e' and press Enter to ENROLL (Wipes DB).");
    ESP_LOGI(TAG, "2. Otherwise, place finger to AUTHENTICATE.");
    ESP_LOGI(TAG, "==============================================\n");

    while (1) {
        // Check for serial monitor input (non-blocking)
        int c = fgetc(stdin);
        if (c == 'e' || c == 'E') {
            ESP_LOGI(TAG, "Database cleared for fresh test.");
            r503_empty_library(&sensor);
            enroll_finger_manual(&sensor, 1);
            ESP_LOGI(TAG, "\nReady to AUTHENTICATE. Place finger on sensor...");
        }

        // Wait for an interrupt OR timeout after 100ms
        uint32_t notification = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));

        // If notification > 0, the ISR woke us up!
        if (notification > 0) {
            ESP_LOGI(TAG, "Touch detected via IRQ! Scanning...");
            authenticate_finger(&sensor);

            // Optional: Wait for the finger to be removed so it doesn't double-trigger
            while(gpio_get_level(FPRINT_TOUCH_IRQ) == 1) {
                vTaskDelay(pdMS_TO_TICKS(50));
            }
        }
    }
}