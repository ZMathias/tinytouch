#include <sys/unistd.h>
#include "ctaphid/ctaphid.h"
#include "ctaphid/ctaphid_constants.h"
#include "usb/usb_setup.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "esp_log.h"
#include <sys/param.h>

enum CTAPHID_STATES {
    IDLE,
    ASSEMBLING
};

const char* TAG = "CTAPHID";

// ==== parse_packet() STATE VARIABLES
static enum CTAPHID_STATES parser_state = IDLE;
static uint32_t active_cid = 0;
static uint8_t active_command = 0;
static uint16_t expected_length = 0;
static uint16_t current_offset = 0;
static uint8_t expected_seq = 0;
static TickType_t last_packet_time = 0;
static uint8_t message[CTAPHID_MAX_MSG_SIZE] = {0};

TimerHandle_t timeout_timer = NULL;


bool eval_timeout() {
    return (xTaskGetTickCount() - last_packet_time) > pdMS_TO_TICKS(500);
}

void packet_timeout_cb(TimerHandle_t xTimer) {
    // start critical section
    assert(xSemaphoreTake(timeout_mutex, portMAX_DELAY) == pdTRUE);
    if (parser_state == ASSEMBLING && eval_timeout()) {
        parser_state = IDLE;
        send_error(active_cid, CTAPHID_ERR_MSG_TIMEOUT);
    }
    xSemaphoreGive(timeout_mutex);
    // end critical section
}

void ctaphid_init_usb() {
#ifdef CONFIG_IDF_TARGET_ESP32S3
    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG();
    tusb_cfg.descriptor.full_speed_config = configuration_descriptor;
    tusb_cfg.descriptor.device = &device_descriptor;
    tusb_cfg.descriptor.string = string_desc_arr;
    tusb_cfg.descriptor.string_count = STRING_DESC_COUNT;

    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));
    ESP_LOGI(TAG, "Initialized FIDO2 USB Endpoint");
#elif CONFIG_IDF_TARGET_LINUX
    xTaskCreate(mock_usb_rx_task, "mock_usb_rx_task", 4096, NULL, 5, NULL);
#endif
}

void ctaphid_init_timeout_eval() {
    timeout_mutex = xSemaphoreCreateMutex();
    timeout_timer = xTimerCreate("CTAP_Packet_Timer", pdMS_TO_TICKS(500), pdTRUE, NULL, packet_timeout_cb);
    xTimerStart(timeout_timer, 0);
}

void ctaphid_init() {
    ctaphid_init_usb();
    ctaphid_init_timeout_eval();
}

bool send_error(const uint32_t cid, const uint8_t cmd) {
    const bool status = send_init(cid, CTAPHID_CMD_ERROR, &cmd, sizeof(cmd));
    return status;
}

bool send_init(const uint32_t cid, const uint8_t cmd, const uint8_t *buf, const uint16_t bufsize) {
    if (bufsize > 57 || (buf == NULL && bufsize > 0)) {
        return false;
    }

    struct CTAPHID_INIT_PACKET packet = {0};
    packet.cid = cid;
    packet.cmd = cmd;
    packet.bcnt = (bufsize << 8) | (bufsize >> 8);

    if (bufsize > 0)
        memcpy(packet.data, buf, bufsize);

    return send_buffer((uint8_t*)&packet);
}

bool send_buffer(const uint8_t *buf) {
#ifdef CONFIG_IDF_TARGET_ESP32S3
    if (buf == NULL)
        return false;
    if (tud_hid_ready()) {
        return tud_hid_report(0, buf, 64);
    }

    return false;
#elif CONFIG_IDF_TARGET_LINUX
    ssize_t bytes_written = write(STDOUT_FILENO, buf, 64);
    if (bytes_written < 0)
        return false;
    return true;
#endif
}

void parse_packet(uint8_t const* buf, const uint16_t bufsize) {
    // state variables are declared at the top of the file

    if (buf == NULL) {
        ESP_LOGI(TAG, "NULL for buf was passed to parse_packet");
        return;
    }
    assert(bufsize == 64); // must always be true

    uint32_t packet_cid = 0;
    memcpy(&packet_cid, buf, sizeof(packet_cid));

    const uint8_t packet_cmd = buf[4];

    // if INIT byte
    if (packet_cmd & 0x80) {
        if (parser_state == IDLE) {
            // wipe state
            parser_state = IDLE;
            active_command = 0;
            expected_length = 0;
            current_offset = 0;
            expected_seq = 0;
            last_packet_time = 0;
            memset(message, 0, CTAPHID_MAX_MSG_SIZE);
        }
        else if (parser_state == ASSEMBLING) {
            if (active_cid == packet_cid) { // INIT override, wipe state
                parser_state = IDLE;
                active_command = 0;
                expected_length = 0;
                current_offset = 0;
                expected_seq = 0;
                last_packet_time = 0;
                memset(message, 0, CTAPHID_MAX_MSG_SIZE);
            } else { // channel hijacking protection
                send_error(packet_cid, CTAPHID_ERR_CHANNEL_BUSY);
                return;
            }
        }

        active_command = packet_cmd;
        active_cid = packet_cid;
        memcpy(&expected_length, buf + 5, sizeof(expected_length));
        expected_length = (expected_length << 8) | (expected_length >> 8); // BCNT is big-endian, swap for little endian

        if (expected_length > CTAPHID_MAX_MSG_SIZE) {
            send_error(active_cid, CTAPHID_ERR_INVALID_LEN);
            parser_state = IDLE;
            return;
        }

        if (expected_length <= 57) { // if the whole data fits into the INIT payload then we are not expecting CONT packets
            parser_state = IDLE;
        } else {
            parser_state = ASSEMBLING; // transition state if we expect more packets
            last_packet_time = xTaskGetTickCount();
        }

        // copy the message
        const uint16_t nr_bytes_to_copy = MIN(57, expected_length);
        assert(CTAPHID_MAX_MSG_SIZE - current_offset >= nr_bytes_to_copy);
        memcpy(message + current_offset, buf + 7, nr_bytes_to_copy);
        current_offset += nr_bytes_to_copy;
        expected_length -= nr_bytes_to_copy; // update the number of remaining bytes to copy in subsequent CONT packets
    }
    else { // if CONT packet
        // only accept packets that have the packet_cid == active_cid while assembling, everything else is dropped silently
        if (parser_state == ASSEMBLING && active_cid == packet_cid) {
            if (eval_timeout()) {
                parser_state = IDLE;
                send_error(active_cid, CTAPHID_ERR_MSG_TIMEOUT);
                return;
            } else {
                last_packet_time = xTaskGetTickCount();
            }

            // TODO: finish cont packet processing
        }
    }
}