#include "serial_status.h"

#include <Arduino.h>

#include "driver/usb_serial_jtag.h"

static bool usb_serial_jtag_ready = false;

void serial_status_init()
{
    if (usb_serial_jtag_is_driver_installed()) {
        usb_serial_jtag_ready = true;
        return;
    }

    usb_serial_jtag_driver_config_t usb_config = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    usb_serial_jtag_ready = usb_serial_jtag_driver_install(&usb_config) == ESP_OK;
}

void serial_status_write_bytes(const uint8_t *data, size_t length)
{
    if (data == NULL || length == 0) {
        return;
    }

    Serial.write(data, length);

    if (!usb_serial_jtag_ready) {
        return;
    }

    usb_serial_jtag_write_bytes(data, length, 0);
}

size_t serial_status_read_usb_bytes(uint8_t *data, size_t max_length)
{
    if (!usb_serial_jtag_ready || data == NULL || max_length == 0) {
        return 0;
    }

    int read_length = usb_serial_jtag_read_bytes(data, max_length, 0);
    if (read_length <= 0) {
        return 0;
    }

    return (size_t)read_length;
}
