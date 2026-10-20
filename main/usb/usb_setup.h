#pragma once
#include "tinyusb.h"
#include "class/hid/hid_device.h"

#ifdef CONFIG_IDF_TARGET_LINUX
#include "freertos/FreeRTOS.h"
#include <unistd.h>

void mock_usb_rx_task(void *pvParameters);
#endif

// FIDO2 mandatory descriptor form
#define TUSB_DESC_TOTAL_LEN      (TUD_CONFIG_DESC_LEN + CFG_TUD_HID * TUD_HID_INOUT_DESC_LEN)
#define EPNUM_HID   0x01
#define STRING_DESC_COUNT 4

extern const uint8_t hid_report_descriptor[];
extern const tusb_desc_device_t device_descriptor;

extern uint8_t configuration_descriptor[];

extern const char* string_desc_arr[];

