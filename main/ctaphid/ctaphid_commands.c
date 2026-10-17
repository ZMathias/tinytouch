#include <stddef.h>
#include "ctaphid/ctaphid_commands.h"
#include "ctaphid/ctaphid.h"
#include "ctaphid/ctaphid_constants.h"
#include "esp_log.h"

static const char *TAG = "CTAPHID_CMD";

// Signature shared by every command handler.
typedef void (*ctaphid_cmd_handler_t)(uint32_t cid, const uint8_t *data, uint16_t len);

// ---------------------------------------------------------------------------
// Per-command handlers. Stubs for now: they only trace the dispatch. Implement
// the actual protocol logic here later; the parser does not need to change.
// ---------------------------------------------------------------------------

static void ctaphid_cmd_init(uint32_t cid, const uint8_t *data, uint16_t len) {
    (void)cid; (void)data; (void)len;
    ESP_LOGD(TAG, "TODO: CTAPHID_INIT (0x%02X)", CTAPHID_CMD_INIT);
}

static void ctaphid_cmd_ping(uint32_t cid, const uint8_t *data, uint16_t len) {
    (void)cid; (void)data; (void)len;
    ESP_LOGD(TAG, "TODO: CTAPHID_PING (0x%02X)", CTAPHID_CMD_PING);
}

static void ctaphid_cmd_msg(uint32_t cid, const uint8_t *data, uint16_t len) {
    (void)cid; (void)data; (void)len;
    ESP_LOGD(TAG, "TODO: CTAPHID_MSG (0x%02X)", CTAPHID_CMD_MSG);
}

static void ctaphid_cmd_lock(uint32_t cid, const uint8_t *data, uint16_t len) {
    (void)cid; (void)data; (void)len;
    ESP_LOGD(TAG, "TODO: CTAPHID_LOCK (0x%02X)", CTAPHID_CMD_LOCK);
}

static void ctaphid_cmd_wink(uint32_t cid, const uint8_t *data, uint16_t len) {
    (void)cid; (void)data; (void)len;
    ESP_LOGD(TAG, "TODO: CTAPHID_WINK (0x%02X)", CTAPHID_CMD_WINK);
}

static void ctaphid_cmd_cbor(uint32_t cid, const uint8_t *data, uint16_t len) {
    (void)cid; (void)data; (void)len;
    ESP_LOGD(TAG, "TODO: CTAPHID_CBOR (0x%02X)", CTAPHID_CMD_CBOR);
}

static void ctaphid_cmd_cancel(uint32_t cid, const uint8_t *data, uint16_t len) {
    (void)cid; (void)data; (void)len;
    ESP_LOGD(TAG, "TODO: CTAPHID_CANCEL (0x%02X)", CTAPHID_CMD_CANCEL);
}

// ---------------------------------------------------------------------------
// Dispatch
// ---------------------------------------------------------------------------

static const struct {
    uint8_t cmd;
    ctaphid_cmd_handler_t handler;
} command_table[] = {
    { CTAPHID_CMD_INIT,   ctaphid_cmd_init   },
    { CTAPHID_CMD_PING,   ctaphid_cmd_ping   },
    { CTAPHID_CMD_MSG,    ctaphid_cmd_msg    },
    { CTAPHID_CMD_LOCK,   ctaphid_cmd_lock   },
    { CTAPHID_CMD_WINK,   ctaphid_cmd_wink   },
    { CTAPHID_CMD_CBOR,   ctaphid_cmd_cbor   },
    { CTAPHID_CMD_CANCEL, ctaphid_cmd_cancel },
};

void ctaphid_execute_command(uint32_t cid, uint8_t cmd, const uint8_t *data, uint16_t len) {
    for (size_t i = 0; i < sizeof(command_table) / sizeof(command_table[0]); ++i) {
        if (command_table[i].cmd == cmd) {
            command_table[i].handler(cid, data, len);
            return;
        }
    }

    // Unknown or host->device-unsupported command (KEEPALIVE/ERROR are device->host only).
    send_error(cid, CTAPHID_ERR_INVALID_CMD);
}
