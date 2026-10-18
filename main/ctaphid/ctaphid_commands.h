#pragma once
#include <stdint.h>

/*
 * CTAPHID command execution backbone.
 *
 * ctaphid_execute_command() is the single entry point the parser calls once a
 * command message has been fully reassembled. It dispatches on the command byte
 * to the per-command handler. The handlers are stubs for now: implement the
 * protocol logic there and leave the parser untouched.
 *
 *   cid  - channel the command arrived on (used to address the response)
 *   cmd  - CTAPHID command byte, one of the CTAPHID_CMD_* constants
 *   data - reassembled payload
 *   len  - payload length in bytes
 */
bool touch_cid(uint32_t cid);
void ctaphid_execute_command(uint32_t cid, uint8_t cmd, const uint8_t *data, uint16_t len);

