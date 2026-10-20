#include <stddef.h>
#include "ctaphid/ctaphid_commands.h"
#include "ctaphid/ctaphid.h"
#include "ctaphid/ctaphid_constants.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "CTAPHID_CMD";

static struct {
    uint32_t cid;
    int64_t last_used;
} active_cids[16] = {};

static constexpr int8_t active_cids_len = sizeof(active_cids) / sizeof(active_cids[0]);
static uint32_t next_cid = 1;

// allocates the next free CID slot or evicts according to LRU
static bool reserve_cid(const uint32_t req_cid) {
    if (req_cid == 0 || req_cid == 0xFFFFFFFF) return false;

    int8_t free_idx = -1;
    int64_t lru = INT64_MAX;
    int8_t lru_idx = -1;
    for (int8_t i = 0; i < active_cids_len; i++) {
        if (active_cids[i].cid == req_cid) {
            // the requested CID is already occupied
            return false;
        }

        if (active_cids[i].cid == 0) {
            free_idx = i;
            break; // exit on the first free slot
        } else if (active_cids[i].last_used < lru) {
            lru = active_cids[i].last_used;
            lru_idx = i;
        }
    }

    if (free_idx == -1) { // if none was found evict according to LRU policy
        free_idx = lru_idx;
    }

    if (free_idx < 0)
        return false;

    // reserve slot
    active_cids[free_idx].cid = req_cid;
    active_cids[free_idx].last_used = esp_timer_get_time();
    return true;
}


static bool search_alloc_cid(const uint32_t cid, uint8_t *idx) {
    if (idx == nullptr) return false;

    for (uint8_t i = 0; i < active_cids_len; i++) {
        if (active_cids[i].cid == cid && active_cids[i].last_used != 0) {
            *idx = i;
            return true;
        }
    }

    return false;
}

bool touch_cid(const uint32_t cid) {
    uint8_t idx;
    if (!search_alloc_cid(cid, &idx)) {
        return false;
    }

    active_cids[idx].last_used = esp_timer_get_time();
    return true;
}

static void ctaphid_cmd_init(uint32_t cid, const uint8_t *data, uint16_t len) {
    if (len != 8) { // if BCNT is not 8
        send_error(cid, CTAPHID_ERR_INVALID_LEN);
        return;
    }

    if (cid != CTAPHID_BROADCAST_CID && !touch_cid(cid)) {
        send_error(cid, CTAPHID_ERR_INVALID_CHANNEL);
        return;
    } // else: parser state reset is already handled in the parser

    // if new CID allocation
    uint32_t req_cid = cid;
    if (cid == CTAPHID_BROADCAST_CID) {
        req_cid = next_cid;
        if (!reserve_cid(req_cid)) {
            send_error(cid, CTAPHID_ERR_OTHER); // couldn't reserve CID
            return;
        }

        next_cid++;
    }

    struct __attribute__((packed)) ctaphid_init_resp_t {
        uint8_t  nonce[8];
        uint32_t cid;
        uint8_t  protocol;
        uint8_t  major;
        uint8_t  minor;
        uint8_t  build;
        uint8_t  caps;
    };
    _Static_assert(sizeof(struct ctaphid_init_resp_t) == 17, "payload must be 17 bytes");

    struct ctaphid_init_resp_t resp = {
        .cid = req_cid,
        .protocol = CTAPHID_PROTOCOL_VERSION,
        .major = CTAPHID_DEV_VER_MAJOR,
        .minor = CTAPHID_DEV_VER_MINOR,
        .build = CTAPHID_DEV_BUILD_VERSION,
        .caps = CTAPHID_CAP_WINK | CTAPHID_CAP_CBOR,
    };

    memcpy(resp.nonce, data, len);
    send_init(cid, CTAPHID_CMD_INIT, (uint8_t*)&resp, sizeof(resp));
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


typedef void (*ctaphid_cmd_handler_t)(uint32_t cid, const uint8_t *data, uint16_t len);

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

void ctaphid_execute_command(const uint32_t cid, const uint8_t cmd, const uint8_t *data, const uint16_t len) {
    for (size_t i = 0; i < sizeof(command_table) / sizeof(command_table[0]); ++i) {
        if (command_table[i].cmd == cmd) {
            command_table[i].handler(cid, data, len);
            return;
        }
    }

    // Unknown or host->device-unsupported command (KEEPALIVE/ERROR are device->host only).
    send_error(cid, CTAPHID_ERR_INVALID_CMD);
}
