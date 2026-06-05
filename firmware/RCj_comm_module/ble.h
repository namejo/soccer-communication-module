#ifndef BLE_H
#define BLE_H

#include <stddef.h>
#include <stdint.h>

int8_t ble_start_server();

int8_t ble_disconnect();

int8_t ble_send_msg(uint8_t *data, size_t length);
int8_t ble_send_log(const char *message);

bool ble_has_pairing_passkey();
uint32_t ble_get_pairing_passkey();

#endif // BLE_H
