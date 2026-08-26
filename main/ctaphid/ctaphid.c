#include <sys/unistd.h>
#include "ctaphid/ctaphid.h"
#include "ctaphid/ctaphid_constants.h"
#include "usb/usb_setup.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

enum CTAPHID_STATES {
    IDLE,
    ASSEMBLING
};

const char* TAG = "CTAPHID";

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

void parse_packet(uint8_t const* buf, uint16_t bufsize) {
    static uint8_t state = IDLE;
    static uint32_t active_cid = 0;
    static uint8_t active_command = 0;
    static uint16_t expected_length = 0;
    static uint16_t current_offset = 0;
    static uint8_t expected_seq = 0;
    static TickType_t last_packet_time = {};
    static uint8_t message[CTAPHID_MAX_MSG_SIZE] = {0};

    if (buf == NULL) {
        ESP_LOGI(TAG, "NULL for buf was passed to parse_packet");
        return;
    }
    assert(bufsize == 64); // must always be true

    uint32_t curr_cid = 0;
    memcpy(&curr_cid, buf, sizeof(curr_cid));

    // if INIT byte
    if (buf[4] & 0x80) {
        state = ASSEMBLING;
        active_cid = curr_cid;
        memcpy(&expected_length, buf + 5, sizeof(expected_length));
        // BCNT is big-endian, swap for little endian
        expected_length = (expected_length << 8) | (expected_length >> 8);

        if (expected_length > CTAPHID_MAX_MSG_SIZE) {
            send_error(active_cid, CTAPHID_ERR_INVALID_LEN);
            return;
        }

        if (expected_length <= 57) { // if the whole data fits into the INIT payload then we are not expecting CONT packets
            state = IDLE;
        } else {
            last_packet_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
        }

        // TODO: extract command, copy BCNT bytes into message and set current_offset to the bytes copied
        // TODO: implement INIT override. If a new INIT packet with the our current active_cid is received while in the ASSEMBLING state discard the old packet
    }
    else { // if CONT packet

        // TODO: check curr_cid against active CID, check if SEQ matches expected seq and copy up to 59 bytes into message + current_offset and increment offset, reset last_packet_time
        // check timeout
        if ((xTaskGetTickCount() * portTICK_PERIOD_MS) - last_packet_time > 500) {
            ESP_LOGI(TAG, "CTAPHID_ERR_MSG_TIMEOUT");
            send_error(active_cid, CTAPHID_ERR_MSG_TIMEOUT);
            return;
        }
    }
}