#include "interbot_comm.h"

#include <Arduino.h>
#include <HardwareSerial.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <atomic>
#include <string.h>

#include "ble.h"
#include "buzzer.h"
#include "definitions.h"
#include "serial_status.h"
#include "sibcp_protocol.h"
#include "state_machine.h"
#include "status_led.h"

#define INTERBOT_RX_QUEUE_LENGTH    8
#define INTERBOT_LOG_QUEUE_LENGTH   12
#define INTERBOT_LOG_LINE_LENGTH    96
#define USB_READ_CHUNK_LENGTH       32
#define INTERBOT_DIAGNOSTICS_INTERVAL_MS 5000u

struct interbot_radio_frame_t {
    uint8_t data[SIBCP_MAX_FRAME_LENGTH];
    uint16_t length;
};

struct interbot_log_item_t {
    char line[INTERBOT_LOG_LINE_LENGTH];
};

struct interbot_diagnostics_t {
    std::atomic<uint32_t> esp_now_init_ok;
    std::atomic<uint32_t> esp_now_init_fail;
    std::atomic<uint32_t> host_tx_attempted;
    std::atomic<uint32_t> host_tx_failed;
    std::atomic<uint32_t> send_cb_ok;
    std::atomic<uint32_t> send_cb_failed;
    std::atomic<uint32_t> radio_rx_valid;
    std::atomic<uint32_t> radio_rx_invalid;
    std::atomic<uint32_t> radio_rx_queue_dropped;
    std::atomic<uint32_t> radio_rx_reserved_dropped;
    std::atomic<uint32_t> host_reserved_dropped;
    std::atomic<uint32_t> radio_forwarded_to_host;
    std::atomic<uint32_t> log_dropped;
};

static const uint8_t broadcast_peer[ESP_NOW_ETH_ALEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

static HardwareSerial interbot_uart(1);
static sibcp_parser_t uart_parser;
static sibcp_parser_t usb_parser;
static QueueHandle_t radio_rx_queue = NULL;
static QueueHandle_t log_queue = NULL;
static bool interbot_ready = false;
static bool local_serial_ready = false;
static uint32_t next_diagnostics_log_ms = 0;
static interbot_diagnostics_t diagnostics = {};

enum local_frame_target_t : uint8_t {
    LOCAL_TARGET_ALL,
    LOCAL_TARGET_UART,
    LOCAL_TARGET_USB,
};

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
        if (xQueueSend(log_queue, &item, 0) != pdTRUE) {
            diagnostics.log_dropped.fetch_add(1, std::memory_order_relaxed);
        }
    }
}

static void queue_text_log(const char *line)
{
    if (log_queue == NULL || line == NULL) {
        return;
    }

    interbot_log_item_t item;
    snprintf(item.line, sizeof(item.line), "%s", line);

    if (xQueueSend(log_queue, &item, 0) != pdTRUE) {
        interbot_log_item_t dropped_item;
        xQueueReceive(log_queue, &dropped_item, 0);
        if (xQueueSend(log_queue, &item, 0) != pdTRUE) {
            diagnostics.log_dropped.fetch_add(1, std::memory_order_relaxed);
        }
    }
}

static void on_esp_now_send(const esp_now_send_info_t *info, esp_now_send_status_t status)
{
    (void)info;

    if (status == ESP_NOW_SEND_SUCCESS) {
        diagnostics.send_cb_ok.fetch_add(1, std::memory_order_relaxed);
    } else {
        diagnostics.send_cb_failed.fetch_add(1, std::memory_order_relaxed);
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
        diagnostics.radio_rx_invalid.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    sibcp_frame_t frame;
    if (!sibcp_validate_frame(data, data_len, &frame)) {
        diagnostics.radio_rx_invalid.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    diagnostics.radio_rx_valid.fetch_add(1, std::memory_order_relaxed);

    interbot_radio_frame_t item;
    memcpy(item.data, data, data_len);
    item.length = data_len;

    if (xQueueSend(radio_rx_queue, &item, 0) != pdTRUE) {
        diagnostics.radio_rx_queue_dropped.fetch_add(1, std::memory_order_relaxed);
    }
}

static void write_local_frame_to(const uint8_t *data, uint16_t length, local_frame_target_t target)
{
    if (data == NULL || length == 0) {
        return;
    }

    if (local_serial_ready && (target == LOCAL_TARGET_ALL || target == LOCAL_TARGET_UART)) {
        interbot_uart.write(data, length);
    }

    if (target == LOCAL_TARGET_ALL || target == LOCAL_TARGET_USB) {
        serial_status_write_bytes(data, length);
    }
}

static void write_local_frame(const uint8_t *data, uint16_t length)
{
    write_local_frame_to(data, length, LOCAL_TARGET_ALL);
}

static esp_err_t configure_esp_now()
{
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false, false);
    WiFi.setSleep(false);

#if SOC_WIFI_SUPPORT_5G
    if (!WiFi.setBandMode(WIFI_BAND_MODE_2G_ONLY)) {
        return ESP_FAIL;
    }
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

    err = esp_now_register_send_cb(on_esp_now_send);
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
    if (interbot_ready) {
        diagnostics.esp_now_init_ok.fetch_add(1, std::memory_order_relaxed);
        queue_text_log("[SIBCP DIAG] ESP-NOW ready");
    } else {
        diagnostics.esp_now_init_fail.fetch_add(1, std::memory_order_relaxed);
        queue_text_log("[SIBCP DIAG] ESP-NOW init failed");
    }

    return err;
}

static void process_host_bytes(
    sibcp_parser_t *parser,
    const uint8_t *data,
    size_t length,
    const char *direction,
    local_frame_target_t response_target
);

static void send_service_status(
    uint16_t transaction_id,
    uint8_t service_id,
    uint8_t status_code,
    local_frame_target_t target
)
{
    sibcp_frame_t response;
    if (!sibcp_build_frame(
        SIBCP_PACKET_SERVICE_RESPONSE,
        transaction_id,
        service_id,
        &status_code,
        sizeof(status_code),
        &response
    )) {
        return;
    }

    write_local_frame_to(response.data, response.length, target);
    queue_log("SYS->HOST", response);
}

static bool handle_system_set_led(const sibcp_frame_t &frame, local_frame_target_t response_target)
{
    if (frame.payload_length != sizeof(sibcp_srv_system_set_led_request_t)) {
        send_service_status(
            frame.transaction_id,
            frame.identifier_id,
            SIBCP_SERVICE_STATUS_ERROR,
            response_target
        );
        return true;
    }

    if (stm_get_state() != STM_PLAY) {
        send_service_status(
            frame.transaction_id,
            frame.identifier_id,
            SIBCP_SERVICE_STATUS_ERROR,
            response_target
        );
        return true;
    }

    sibcp_srv_system_set_led_request_t request;
    memcpy(&request, &frame.data[SIBCP_HEADER_LENGTH], sizeof(request));

    bool ok = status_led_set_custom(
        request.mode,
        request.red,
        request.green,
        request.blue,
        request.duration_ms
    );

    send_service_status(
        frame.transaction_id,
        frame.identifier_id,
        ok ? SIBCP_SERVICE_STATUS_OK : SIBCP_SERVICE_STATUS_ERROR,
        response_target
    );
    return true;
}

static bool handle_system_play_melody(const sibcp_frame_t &frame, local_frame_target_t response_target)
{
    if (frame.payload_length != sizeof(sibcp_srv_system_play_melody_request_t)) {
        send_service_status(
            frame.transaction_id,
            frame.identifier_id,
            SIBCP_SERVICE_STATUS_ERROR,
            response_target
        );
        return true;
    }

    sibcp_srv_system_play_melody_request_t request;
    memcpy(&request, &frame.data[SIBCP_HEADER_LENGTH], sizeof(request));

    bool ok = buzzer_play_melody(request.melody_id, request.repeat);
    send_service_status(
        frame.transaction_id,
        frame.identifier_id,
        ok ? SIBCP_SERVICE_STATUS_OK : SIBCP_SERVICE_STATUS_ERROR,
        response_target
    );
    return true;
}

static bool handle_local_system_service(const sibcp_frame_t &frame, local_frame_target_t response_target)
{
    if (frame.packet_type != SIBCP_PACKET_SERVICE_REQUEST) {
        return false;
    }

    switch (frame.identifier_id) {
        case SIBCP_SERVICE_SYSTEM_SET_LED:
            return handle_system_set_led(frame, response_target);
        case SIBCP_SERVICE_SYSTEM_PLAY_MELODY:
            return handle_system_play_melody(frame, response_target);
        default:
            return false;
    }
}

static void process_host_bytes(
    sibcp_parser_t *parser,
    const uint8_t *data,
    size_t length,
    const char *direction,
    local_frame_target_t response_target
)
{
    for (size_t i = 0; i < length; i++) {
        sibcp_frame_t frame;

        if (sibcp_parser_push_byte(parser, data[i], &frame)) {
            if (handle_local_system_service(frame, response_target)) {
                queue_log("HOST->SYS", frame);
                continue;
            }

            if (sibcp_is_reserved_external_frame(&frame)) {
                diagnostics.host_reserved_dropped.fetch_add(1, std::memory_order_relaxed);
                queue_log("HOST->DROP", frame);
                continue;
            }

            if (interbot_ready) {
                diagnostics.host_tx_attempted.fetch_add(1, std::memory_order_relaxed);
                esp_err_t send_result = esp_now_send(broadcast_peer, frame.data, frame.length);
                if (send_result != ESP_OK) {
                    diagnostics.host_tx_failed.fetch_add(1, std::memory_order_relaxed);
                    queue_text_log("[SIBCP DIAG] ESP-NOW send failed");
                }
            } else {
                diagnostics.host_tx_failed.fetch_add(1, std::memory_order_relaxed);
            }
            queue_log(direction, frame);
        }
    }
}

static void process_uart_input()
{
    while (interbot_uart.available() > 0) {
        uint8_t byte = (uint8_t)interbot_uart.read();
        process_host_bytes(&uart_parser, &byte, 1, "UART->NOW", LOCAL_TARGET_UART);
    }
}

static void process_usb_input()
{
    uint8_t buffer[USB_READ_CHUNK_LENGTH];
    size_t read_length = 0;

    do {
        read_length = serial_status_read_usb_bytes(buffer, sizeof(buffer));
        process_host_bytes(&usb_parser, buffer, read_length, "USB->NOW", LOCAL_TARGET_USB);
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

        if (sibcp_is_reserved_external_frame(&frame)) {
            diagnostics.radio_rx_reserved_dropped.fetch_add(1, std::memory_order_relaxed);
            queue_log("NOW->DROP", frame);
            continue;
        }

        write_local_frame(item.data, item.length);
        diagnostics.radio_forwarded_to_host.fetch_add(1, std::memory_order_relaxed);
        queue_log("NOW->HOST", frame);
    }
}

static void maybe_log_diagnostics()
{
    const uint32_t now = millis();
    if (next_diagnostics_log_ms != 0 && (int32_t)(now - next_diagnostics_log_ms) < 0) {
        return;
    }
    next_diagnostics_log_ms = now + INTERBOT_DIAGNOSTICS_INTERVAL_MS;

    char line[INTERBOT_LOG_LINE_LENGTH];
    snprintf(
        line,
        sizeof(line),
        "[SIBCP DIAG] ready=%u tx=%lu fail=%lu cb_ok=%lu cb_fail=%lu rx=%lu invalid=%lu qdrop=%lu hdrop=%lu rdrop=%lu",
        interbot_ready ? 1u : 0u,
        (unsigned long)diagnostics.host_tx_attempted.load(std::memory_order_relaxed),
        (unsigned long)diagnostics.host_tx_failed.load(std::memory_order_relaxed),
        (unsigned long)diagnostics.send_cb_ok.load(std::memory_order_relaxed),
        (unsigned long)diagnostics.send_cb_failed.load(std::memory_order_relaxed),
        (unsigned long)diagnostics.radio_rx_valid.load(std::memory_order_relaxed),
        (unsigned long)diagnostics.radio_rx_invalid.load(std::memory_order_relaxed),
        (unsigned long)diagnostics.radio_rx_queue_dropped.load(std::memory_order_relaxed),
        (unsigned long)diagnostics.host_reserved_dropped.load(std::memory_order_relaxed),
        (unsigned long)diagnostics.radio_rx_reserved_dropped.load(std::memory_order_relaxed)
    );
    queue_text_log(line);
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
    maybe_log_diagnostics();
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

void interbot_comm_send_system_score(uint8_t own_score, uint8_t opponent_score)
{
    sibcp_msg_system_score_t payload;
    payload.own_score = own_score;
    payload.opponent_score = opponent_score;

    sibcp_frame_t frame;
    if (!sibcp_build_frame(
        SIBCP_PACKET_TOPIC,
        0,
        SIBCP_TOPIC_SYSTEM_SCORE,
        (const uint8_t *)&payload,
        sizeof(payload),
        &frame
    )) {
        return;
    }

    write_local_frame(frame.data, frame.length);
    queue_log("SYS->HOST", frame);
}

void interbot_comm_send_system_match_time(uint8_t half, uint32_t remaining_ms, uint32_t phase_total_ms)
{
    sibcp_msg_system_match_time_t payload;
    payload.half = half;
    payload.remaining_ms = remaining_ms;
    payload.phase_total_ms = phase_total_ms;

    sibcp_frame_t frame;
    if (!sibcp_build_frame(
        SIBCP_PACKET_TOPIC,
        0,
        SIBCP_TOPIC_SYSTEM_MATCH_TIME,
        (const uint8_t *)&payload,
        sizeof(payload),
        &frame
    )) {
        return;
    }

    write_local_frame(frame.data, frame.length);
    queue_log("SYS->HOST", frame);
}

void interbot_comm_send_system_referee_event(uint8_t event)
{
    sibcp_msg_system_referee_event_t payload;
    payload.event = event;

    sibcp_frame_t frame;
    if (!sibcp_build_frame(
        SIBCP_PACKET_TOPIC,
        0,
        SIBCP_TOPIC_SYSTEM_REFEREE_EVENT,
        (const uint8_t *)&payload,
        sizeof(payload),
        &frame
    )) {
        return;
    }

    write_local_frame(frame.data, frame.length);
    queue_log("SYS->HOST", frame);
}
