#pragma once
#include <stdint.h>
#include "freertos/FreeRTOS.h"

enum ctap_event_type_enum {
    CTAP_EV_PACKET, CTAP_EV_TIMEOUT
};

struct CTAP_REPORT_T {
    uint8_t type;
    uint8_t buf[64];
};

struct __attribute__((packed)) CTAPHID_INIT_PACKET {
    uint32_t cid;
    uint8_t cmd;
    uint16_t bcnt;
    uint8_t data[57];
};

struct __attribute__((packed)) CTAPHID_CONT_PACKET {
    uint32_t cid;
    uint8_t cmd;
    uint8_t data[59];
};

extern QueueHandle_t reportQueue;

void ctaphid_init();
void parse_packet(const struct CTAP_REPORT_T *report);
bool send_buffer(const uint8_t *buf);
bool send_init(uint32_t cid, uint8_t cmd, const uint8_t *buf, uint16_t bufsize);
bool send_error(uint32_t cid, uint8_t cmd);

