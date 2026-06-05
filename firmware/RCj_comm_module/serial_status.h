#ifndef SERIAL_STATUS_H
#define SERIAL_STATUS_H

#include <stddef.h>
#include <stdint.h>

void serial_status_init();
void serial_status_write_bytes(const uint8_t *data, size_t length);
size_t serial_status_read_usb_bytes(uint8_t *data, size_t max_length);

#endif // SERIAL_STATUS_H
