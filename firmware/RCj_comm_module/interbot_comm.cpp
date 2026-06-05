#include "interbot_comm.h"

#include <Arduino.h>
#include <HardwareSerial.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <string.h>

#include "ble.h"
#include "definitions.h"
#include "serial_status.h"
#include "sibcp_protocol.h"

#define INTERBOT_RX_QUEUE_LENGTH    8
#define INTERBOT_LOG_QUEUE_LENGTH   12
#define INTERBOT_LOG_LINE_LENGTH    96
#define USB_READ_CHUNK_LENGTH       32

struct interbot_radio_frame_t {
    uint8_t data[SIBCP_MAX_FRAME_LENGTH];
    uint16_t length;
};

struct interbot_log_item_t {
    char line[INTERBOT_LOG_LINE_LENGTH];
};

static const uint8_t broadcast_peer[ESP_NOW_ETH_ALEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

static HardwareSerial interbot_uart(1);
static sibcp_parser_t uart_parser;
static sibcp_parser_t usb_parser;
static QueueHandle_t radio_rx_queue = NULL;
static QueueHandle_t log_queue = NULL;
static bool interbot_ready = false;
static bool local_serial_ready = false;

static void queue_log(const char *direction, const sibcp_frame_t &frame)
{
    if (log_queue == NULL) {
        return;
    }

    interbot_log_item_t item;
    snprintf(
        item.line,
        sizeof(item.line),
        "[SIBCP %s] %s id=%u tx=%u len=%u",
        direction,
        sibcp_packet_type_name(frame.packet_type),
        frame.identifier_id,
        frame.transaction_id,
        frame.payload_length
    );

    if (xQueueSend(log_queue, &item, 0) != pdTRUE) {
        interbot_log_item_t dropped_item;
        xQueueReceive(log_queue, &dropped_item, 0);
        xQueueSend(log_queue, &item, 0);
    }
}

static void on_esp_now_receive(const esp_now_recv_info_t *info, const uint8_t *data, int data_len)
{
    (void)info;

    if (
        radio_rx_queue == NULL ||
        data == NULL ||
        data_len <= 0 ||
        data_len > SIBCP_MAX_FRAME_LENGTH
    ) {
        return;
    }

    sibcp_frame_t frame;
    if (!sibcp_validate_frame(data, data_len, &frame)) {
        return;
    }

    interbot_radio_frame_t item;
    memcpy(item.data, data, data_len);
    item.length = data_len;

    xQueueSend(radio_rx_queue, &item, 0);
}

static void write_local_frame(const uint8_t *data, uint16_t length)
{
    if (data == NULL || length == 0) {
        return;
    }

    if (local_serial_ready) {
        interbot_uart.write(data, length);
    }

    serial_status_write_bytes(data, length);
}

static esp_err_t configure_esp_now()
{
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false, false);
    WiFi.setSleep(false);

#if SOC_WIFI_SUPPORT_5G
    if (!WiFi.setBandMode(WIFI_BAND_MODE_5G_ONLY)) {
        return ESP_FAIL;
    }
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif

    int channel_result = WiFi.setChannel(INTERBOT_WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
    if (channel_result != ESP_OK) {
        return (esp_err_t)channel_result;
    }

    esp_err_t err = esp_now_init();
    if (err != ESP_OK) {
        return err;
    }

    err = esp_now_register_recv_cb(on_esp_now_receive);
    if (err != ESP_OK) {
        return err;
    }

    esp_now_peer_info_t peer;
    memset(&peer, 0, sizeof(peer));
    memcpy(peer.peer_addr, broadcast_peer, ESP_NOW_ETH_ALEN);
    peer.channel = INTERBOT_WIFI_CHANNEL;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;

    if (!esp_now_is_peer_exist(broadcast_peer)) {
        err = esp_now_add_peer(&peer);
        if (err != ESP_OK) {
            return err;
        }
    }

    return ESP_OK;
}

esp_err_t interbot_comm_init()
{
    sibcp_parser_reset(&uart_parser);
    sibcp_parser_reset(&usb_parser);

    radio_rx_queue = xQueueCreate(INTERBOT_RX_QUEUE_LENGTH, sizeof(interbot_radio_frame_t));
    log_queue = xQueueCreate(INTERBOT_LOG_QUEUE_LENGTH, sizeof(interbot_log_item_t));

    if (radio_rx_queue == NULL || log_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    interbot_uart.begin(
        INTERBOT_UART_SPEED,
        SERIAL_8N1,
        INTERBOT_UART_RX_GPIO,
        INTERBOT_UART_TX_GPIO
    );
    local_serial_ready = true;

    esp_err_t err = configure_esp_now();
    interbot_ready = err == ESP_OK;

    return err;
}

static void process_host_bytes(
    sibcp_parser_t *parser,
    const uint8_t *data,
    size_t length,
    const char *direction
)
{
    for (size_t i = 0; i < length; i++) {
        sibcp_frame_t frame;

        if (sibcp_parser_push_byte(parser, data[i], &frame)) {
            if (interbot_ready) {
                esp_now_send(broadcast_peer, frame.data, frame.length);
            }
            queue_log(direction, frame);
        }
    }
}

static void process_uart_input()
{
    while (interbot_uart.available() > 0) {
        uint8_t byte = (uint8_t)interbot_uart.read();
        process_host_bytes(&uart_parser, &byte, 1, "UART->NOW");
    }
}

static void process_usb_input()
{
    uint8_t buffer[USB_READ_CHUNK_LENGTH];
    size_t read_length = 0;

    do {
        read_length = serial_status_read_usb_bytes(buffer, sizeof(buffer));
        process_host_bytes(&usb_parser, buffer, read_length, "USB->NOW");
    } while (read_length == sizeof(buffer));
}

static void process_radio_input()
{
    interbot_radio_frame_t item;

    while (xQueueReceive(radio_rx_queue, &item, 0) == pdTRUE) {
        sibcp_frame_t frame;
        if (!sibcp_validate_frame(item.data, item.length, &frame)) {
            continue;
        }

        write_local_frame(item.data, item.length);
        queue_log("NOW->HOST", frame);
    }
}

static void process_log_output()
{
    interbot_log_item_t item;

    while (xQueueReceive(log_queue, &item, 0) == pdTRUE) {
        ble_send_log(item.line);
    }
}

void interbot_comm_update()
{
    process_uart_input();
    process_usb_input();
    process_radio_input();
    process_log_output();
}

void interbot_comm_send_system_game_state(uint8_t state, bool robot_play)
{
    sibcp_msg_system_game_state_t payload;
    payload.state = state;
    payload.robot_play = robot_play ? 1 : 0;

    sibcp_frame_t frame;
    if (!sibcp_build_frame(
        SIBCP_PACKET_TOPIC,
        0,
        SIBCP_TOPIC_SYSTEM_GAME_STATE,
        (const uint8_t *)&payload,
        sizeof(payload),
        &frame
    )) {
        return;
    }

    write_local_frame(frame.data, frame.length);
    queue_log("SYS->HOST", frame);
}
