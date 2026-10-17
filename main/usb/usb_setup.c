#include "usb_setup.h"
#include "ctaphid/ctaphid.h"

const uint8_t hid_report_descriptor[] = {
    0x06, 0xD0, 0xF1, // Usage Page (FIDO Alliance)
    0x09, 0x01,       // Usage (U2F HID Authenticator Device)
    0xA1, 0x01,       // Collection (Application)
    0x09, 0x20,       //   Usage (Input Report Data)
    0x15, 0x00,       //   Logical Minimum (0)
    0x26, 0xFF, 0x00, //   Logical Maximum (255)
    0x75, 0x08,       //   Report Size (8)
    0x95, 0x40,       //   Report Count (64) - CT APHID 64-byte packets
    0x81, 0x02,       //   Input (Data, Var, Abs)
    0x09, 0x21,       //   Usage (Output Report Data)
    0x15, 0x00,       //   Logical Minimum (0)
    0x26, 0xFF, 0x00, //   Logical Maximum (255)
    0x75, 0x08,       //   Report Size (8)
    0x95, 0x40,       //   Report Count (64)
    0x91, 0x02,       //   Output (Data, Var, Abs)
    0xC0
};

const tusb_desc_device_t device_descriptor = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,         // USB 2.0
    .bDeviceClass = 0x00,     // Class defined at interface level
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = 0xCAFE,
    .idProduct = 0x4002,
    .bcdDevice = 0x0100,
    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x03,
    .bNumConfigurations = 0x01
};

#define EPNUM_HID   0x01
uint8_t configuration_descriptor[] = {
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, TUSB_DESC_TOTAL_LEN, 0, 100),
    TUD_HID_INOUT_DESCRIPTOR(0, 0, HID_ITF_PROTOCOL_NONE, sizeof(hid_report_descriptor),
                             EPNUM_HID, EPNUM_HID | 0x80, 64, 10)
};

const char* string_desc_arr[] = {
    (const char[]) { 0x09, 0x04 }, // 0: Supported language is English (0x0409)
    "MathiasRex",                   // 1: Manufacturer
    "FIDO2 Fingerprint Authenticator",              // 2: Product
    "0000000001"                   // 3: Serials
};

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) {
    return hid_report_descriptor;
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t* buffer, uint16_t reqlen) {
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const* buffer, uint16_t bufsize) {
    (void)instance; // silences unused param warnings
    if (report_id != 0) return;
    if (report_type != HID_REPORT_TYPE_OUTPUT) return;
    if (bufsize != 64) return;

    // package it into the struct
    struct CTAP_REPORT_T report;
    memcpy(report.buf, buffer, bufsize);
    report.type = CTAP_EV_PACKET;

    if (xQueueSend(reportQueue, &report, 0) != pdPASS) {
        // queue full, drop
    }
}

#ifdef CONFIG_IDF_TARGET_LINUX
void mock_usb_rx_task(void *pvParameters) {
    uint8_t buf[64];

    while (true) {
        memset(buf, 0, sizeof(buf));
        const ssize_t bytes_read = read(STDIN_FILENO, buf, 64);

        if (bytes_read > 0) {
            // package it into the struct, same as the USB callback
            struct CTAP_REPORT_T report;
            memcpy(report.buf, buf, sizeof(report.buf));
            report.type = CTAP_EV_PACKET;

            if (xQueueSend(reportQueue, &report, 0) != pdPASS) {
                // queue full, drop
            }
        } else if (bytes_read == 0) {
            // pipe EOF
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
}

#endif