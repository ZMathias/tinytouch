#pragma once

#include <stdint.h>

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

void ctaphid_init_usb();
void parse_packet(uint8_t const* buf, uint16_t bufsize);
bool send_buffer(const uint8_t *buf);
bool send_init(const uint32_t cid, const uint8_t cmd, const uint8_t *buf, const uint16_t bufsize);

