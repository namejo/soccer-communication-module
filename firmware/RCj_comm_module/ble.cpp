#include <cstddef>
#include <string.h>
#include <Arduino.h>
#include "HardwareSerial.h"
#include <sys/_stdint.h>
#include "WString.h"
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <BLESecurity.h>
#include "esp_random.h"

#include "definitions.h"
#include "functions.h"
#include "state_machine.h"
#include "ble_processing.h"
#include "ble.h"

// See the following for generating UUIDs:
// https://www.uuidgenerator.net/
#define SERVICE_UUID           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E" // UART service UUID
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_LOG "6E400004-B5A3-F393-E0A9-E50E24DCCA9E"

static BLEServer *pServer;
static BLECharacteristic *pRxCharacteristic;
static BLECharacteristic *pTxCharacteristic;
static BLECharacteristic *pLogCharacteristic;
static bool device_connected = false;
static bool ble_authenticated = false;
static bool pairing_passkey_ready = false;
static uint32_t pairing_passkey = 0;
static String receive_data;
static QueueHandle_t ble_msg_queue;

static uint32_t generate_pairing_passkey() {
    return esp_random() % 1000000U;
}

static void set_pairing_passkey(uint32_t passkey) {
    pairing_passkey = passkey % 1000000U;
    pairing_passkey_ready = true;
}

#if defined(CONFIG_NIMBLE_ENABLED)
static bool connection_is_authenticated(const ble_gap_conn_desc *desc) {
    return desc != NULL &&
           desc->sec_state.encrypted == 1 &&
           desc->sec_state.authenticated == 1 &&
           desc->sec_state.key_size >= 16;
}
#endif

static bool decode_ble_msg(const String &data, ble_msg_t *msg) {
    if (msg == NULL ||
        data.length() == 0 ||
        data.length() > BLE_DATA_MAX_LENGTH + 1) {
        return false;
    }

    uint8_t msg_id = (uint8_t) data[0];
    if (msg_id >= BLE_MSG_MAX_ID) {
        return false;
    }

    *msg = {};
    msg->msg_id = msg_id;
    msg->data_length = data.length() - 1;  // -1 for the message ID

    for (uint8_t i = 0; i < msg->data_length; i++) {
        msg->data[i] = (uint8_t) data[i + 1];
    }

    return true;
}

static bool queue_ble_msg(const ble_msg_t &msg) {
    if (xQueueSend(ble_msg_queue, (void *) &msg, 0) == pdTRUE) {
        return true;
    }

    ble_msg_t dropped_msg;
    xQueueReceive(ble_msg_queue, &dropped_msg, 0);
    return xQueueSend(ble_msg_queue, (void *) &msg, 0) == pdTRUE;
}

class RcjBleSecurityCallbacks: public BLESecurityCallbacks {
    uint32_t onPassKeyRequest() override {
        return pairing_passkey;
    }

    void onPassKeyNotify(uint32_t pass_key) override {
        set_pairing_passkey(pass_key);
    }

    bool onSecurityRequest() override {
        return true;
    }

    bool onConfirmPIN(uint32_t pin) override {
        set_pairing_passkey(pin);
        return false;
    }

    bool onAuthorizationRequest(uint16_t connHandle, uint16_t attrHandle, bool isRead) override {
        (void) connHandle;
        (void) attrHandle;
        (void) isRead;
        return ble_authenticated;
    }

#if defined(CONFIG_BLUEDROID_ENABLED)
    void onAuthenticationComplete(esp_ble_auth_cmpl_t desc) override {
        ble_authenticated = desc.success;
    }
#endif

#if defined(CONFIG_NIMBLE_ENABLED)
    void onAuthenticationComplete(ble_gap_conn_desc *desc) override {
        ble_authenticated = connection_is_authenticated(desc);
    }
#endif
};

static void configure_ble_security() {
    static BLESecurity security;
    static RcjBleSecurityCallbacks security_callbacks;

    BLEDevice::setSecurityCallbacks(&security_callbacks);
    BLESecurity::setAuthenticationMode(ESP_LE_AUTH_REQ_SC_MITM_BOND);
    BLESecurity::setCapability(ESP_IO_CAP_OUT);
    BLESecurity::setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
    BLESecurity::setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
    BLESecurity::setKeySize(16);
    set_pairing_passkey(BLESecurity::setPassKey(true, generate_pairing_passkey()));
    BLESecurity::regenPassKeyOnConnect(false);
    BLESecurity::setForceAuthentication(true);
}


// Callbacks
class MyServerCallbacks: public BLEServerCallbacks {
    //void onConnect(BLEServer* pServer, esp_ble_gatts_cb_param_t *param) override {
    void onConnect(BLEServer* pServer) override {
        //Serial.println("Client connected");
        device_connected = true;
        ble_authenticated = false;
        //stm_set_state(STM_PLAY);
    }

#if defined(CONFIG_NIMBLE_ENABLED)
    void onConnect(BLEServer* pServer, ble_gap_conn_desc *desc) override {
        device_connected = true;
        ble_authenticated = connection_is_authenticated(desc);
        pServer->requestConnParams(
            desc->conn_handle,
            BLE_CONN_INTERVAL_MIN,
            BLE_CONN_INTERVAL_MAX,
            BLE_CONN_LATENCY,
            BLE_CONN_TIMEOUT
        );
    }
#endif

    void onDisconnect(BLEServer* pServer) override {
        //Serial.println("Client disconnected");
        device_connected = false;
        ble_authenticated = false;
        stm_set_state(STM_DISCONNECTED);
        // Start advertising
        pServer->getAdvertising()->start();
    }
};

class MyCallbacks: public BLECharacteristicCallbacks {
    void handleWrite(BLECharacteristic *pCharacteristic) {
        // Get received data
        receive_data = pCharacteristic->getValue();

        if (pCharacteristic == pRxCharacteristic) {
            ble_msg_t msg;
            if (decode_ble_msg(receive_data, &msg)) {
                queue_ble_msg(msg);
            }
        }
    }

    void onWrite(BLECharacteristic *pCharacteristic) override {
        if (!ble_authenticated) {
            return;
        }

        handleWrite(pCharacteristic);
    }

#if defined(CONFIG_NIMBLE_ENABLED)
    void onWrite(BLECharacteristic *pCharacteristic, ble_gap_conn_desc *desc) override {
        ble_authenticated = connection_is_authenticated(desc);
        if (!ble_authenticated) {
            return;
        }

        handleWrite(pCharacteristic);
    }
#endif
};

int8_t ble_start_server() {

    ble_msg_processing_init();

    // Assign message queue
    ble_msg_queue = ble_msg_proccesing_get_queue();

    // Create the BLE Device
    BLEDevice::init(BLE_NAME);
    configure_ble_security();

    // Create the BLE Server
    pServer = BLEDevice::createServer();

    // Server callback
    pServer->setCallbacks(new MyServerCallbacks());
  
    // Create the BLE Service
    BLEService *pService = pServer->createService(SERVICE_UUID);

    // Create write characteristic
    pRxCharacteristic = pService->createCharacteristic(
                                            CHARACTERISTIC_UUID_RX,
                                            BLECharacteristic::PROPERTY_WRITE |
                                            BLECharacteristic::PROPERTY_WRITE_NR |
                                            BLECharacteristic::PROPERTY_WRITE_AUTHEN
                                            );  
    pRxCharacteristic->setAccessPermissions(ESP_GATT_PERM_WRITE_ENC_MITM);
    pRxCharacteristic->setCallbacks(new MyCallbacks()); 

    // Create notify (read) characteristic
    pTxCharacteristic = pService->createCharacteristic(
                                            CHARACTERISTIC_UUID_TX,
                                            BLECharacteristic::PROPERTY_NOTIFY
                                            );

    pLogCharacteristic = pService->createCharacteristic(
                                            CHARACTERISTIC_UUID_LOG,
                                            BLECharacteristic::PROPERTY_NOTIFY
                                            );

    // Start the service
    pService->start();

    // Start advertising
    pServer->getAdvertising()->start();

    return ESP_OK;
}

int8_t ble_disconnect() {
    if (device_connected) {
        ble_msg_t send_msg;

        send_msg.msg_id = BLE_MSG_DISCONNECT;
        ble_send_msg((uint8_t*) &send_msg, 1);

        pServer->disconnect(pServer->getConnId());
    }
    return ESP_OK;
}

bool ble_has_pairing_passkey() {
    return pairing_passkey_ready;
}

uint32_t ble_get_pairing_passkey() {
    return pairing_passkey;
}

int8_t ble_send_msg(uint8_t *data, size_t length) {
    pTxCharacteristic->setValue(data, length);
    //Serial.println("Data set");
    pTxCharacteristic->notify();
    //Serial.println("Data send");

    return ESP_OK;
}

int8_t ble_send_log(const char *message) {
    if (!device_connected || pLogCharacteristic == NULL || message == NULL) {
        return ESP_OK;
    }

    pLogCharacteristic->setValue((uint8_t *)message, strlen(message));
    pLogCharacteristic->notify();

    return ESP_OK;
}
