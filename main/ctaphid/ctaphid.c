#include <sys/unistd.h>
#include "ctaphid/ctaphid.h"
#include "ctaphid/ctaphid_commands.h"
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

#define CTAPHID_TIMEOUT_MS 500
static TimerHandle_t timeoutTimer;

const char* TAG = "CTAPHID";
QueueHandle_t reportQueue;

// ==== parse_packet() STATE VARIABLES
static enum CTAPHID_STATES parser_state = IDLE;
static uint32_t active_cid = 0;
static uint8_t active_command = 0;
static uint16_t remaining = 0;
static uint16_t current_offset = 0;
static uint8_t expected_seq = 0;
static uint8_t message[CTAPHID_MAX_MSG_SIZE] = {0};

static void ctaphid_timeout_cb(TimerHandle_t xTimer) {
    (void)xTimer;
    struct CTAP_REPORT_T report;
    report.type = CTAP_EV_TIMEOUT;
    xQueueSend(reportQueue, &report, 0);
}

void ctaphid_timer_init(void) {
    timeoutTimer = xTimerCreate(
        "ctaphid_timeout_timer",
        pdMS_TO_TICKS(CTAPHID_TIMEOUT_MS),
        pdFALSE,
        NULL,
        ctaphid_timeout_cb);
    configASSERT(timeoutTimer);
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

void ctaphid_init() {
    ctaphid_timer_init();
    ctaphid_init_usb();
}

bool send_error(const uint32_t cid, const uint8_t cmd) {
    const bool status = send_init(cid, CTAPHID_CMD_ERROR, &cmd, sizeof(cmd));
    return status;
}

bool send_init(const uint32_t cid, const uint8_t cmd, const uint8_t *buf, const uint16_t bufsize) {
    if (bufsize > CTAPHID_INIT_PAYLOAD_SIZE || (buf == NULL && bufsize > 0)) {
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

// Clear all transaction state. Used to begin or abort a transaction.
static void reset_parser_state(void) {
    parser_state = IDLE;
    active_cid = 0;
    active_command = 0;
    remaining = 0;
    current_offset = 0;
    expected_seq = 0;
    memset(message, 0, sizeof(message));
    xTimerStop(timeoutTimer, 0);
}

// Abort the transaction in progress: stop the timeout and report the error on the active channel.
static void abort_transaction(const uint8_t err) {
    xTimerStop(timeoutTimer, 0);
    send_error(active_cid, err);
    reset_parser_state();
}

void parse_packet(const struct CTAP_REPORT_T * const report) {
    // state variables are declared at the top of the file
    if (report == nullptr) {
        ESP_LOGI(TAG, "nullptr was passed to parse_packet");
        return;
    }

    if (report->type == CTAP_EV_TIMEOUT) {
        if (parser_state == IDLE) return;              // transaction already finished
        if (xTimerIsTimerActive(timeoutTimer)) return; // a packet re-armed the timer, this event is stale

        abort_transaction(CTAPHID_ERR_MSG_TIMEOUT);
        return;
    }

    uint32_t packet_cid = 0;
    memcpy(&packet_cid, report->buf, sizeof(packet_cid));

    const uint8_t packet_cmd = report->buf[4];

    // if INIT byte
    if (packet_cmd & CTAPHID_TYPE_INIT) {
        if (parser_state == IDLE) {
            reset_parser_state();
        }
        else if (parser_state == ASSEMBLING) {
            if (active_cid == packet_cid) { // INIT override: restart the transaction on this channel
                reset_parser_state();
            } else { // channel hijacking protection: leave the in-flight transaction untouched
                send_error(packet_cid, CTAPHID_ERR_CHANNEL_BUSY);
                return;
            }
        }

        active_command = packet_cmd; // raw wire byte, bit7 = INIT, matches the CTAPHID_CMD_* constants
        active_cid = packet_cid;
        memcpy(&remaining, report->buf + 5, sizeof(remaining));
        remaining = (remaining << 8) | (remaining >> 8); // BCNT is big-endian, swap for little endian

        if (remaining > CTAPHID_MAX_MSG_SIZE) {
            abort_transaction(CTAPHID_ERR_INVALID_LEN);
            return;
        }

        if (remaining <= CTAPHID_INIT_PAYLOAD_SIZE) { // whole payload fits in the INIT packet, no CONT expected
            parser_state = IDLE;
        } else {
            parser_state = ASSEMBLING; // transition state if we expect more packets
        }

        // copy the message
        const uint16_t nr_bytes_to_copy = MIN(CTAPHID_INIT_PAYLOAD_SIZE, remaining);

        if (nr_bytes_to_copy > CTAPHID_MAX_MSG_SIZE - current_offset) {
            abort_transaction(CTAPHID_ERR_OTHER);
            return;
        }
        memcpy(message + current_offset, report->buf + 7, nr_bytes_to_copy);
        current_offset += nr_bytes_to_copy;
        remaining -= nr_bytes_to_copy; // update the number of remaining bytes to copy in subsequent CONT packets

        if (parser_state == IDLE) {
            // the INIT packet was the sole packet, transaction complete -> disarm the timeout
            xTimerStop(timeoutTimer, 0);
            ctaphid_execute_command(active_cid, active_command, message, current_offset);
        } else {
            // more fragments expected -> arm the timeout
            xTimerReset(timeoutTimer, 0);
        }
    }
    else { // if CONT packet
        // only accept packets for the active channel while assembling, everything else is dropped silently
        if (parser_state == ASSEMBLING && active_cid == packet_cid) {
            const uint8_t packet_seq = report->buf[4]; // CONT byte 4 carries the sequence number (bit7 reserved, always 0)

            if (packet_seq != expected_seq) { // dropped, reordered or duplicated fragment
                abort_transaction(CTAPHID_ERR_INVALID_SEQ);
                return;
            }

            // copy the payload (CONT header is 5 bytes: 4 CID + 1 SEQ)
            const uint16_t nr_bytes_to_copy = MIN(CTAPHID_CONT_PAYLOAD_SIZE, remaining);
            // runtime bound: assert() is compiled out under NDEBUG, so keep a real check.
            if (nr_bytes_to_copy > CTAPHID_MAX_MSG_SIZE - current_offset) {
                abort_transaction(CTAPHID_ERR_OTHER);
                return;
            }
            memcpy(message + current_offset, report->buf + 5, nr_bytes_to_copy);
            current_offset += nr_bytes_to_copy;
            remaining -= nr_bytes_to_copy;
            expected_seq++;

            if (remaining == 0) {
                // last fragment, message complete -> disarm the timeout, keep the message for dispatch
                xTimerStop(timeoutTimer, 0);
                parser_state = IDLE;
                ctaphid_execute_command(active_cid, active_command, message, current_offset);
            } else {
                // more fragments expected -> (re)arm the timeout for the next CONT packet
                xTimerReset(timeoutTimer, 0);
            }
        }
    }
}