#include <Arduino.h>
#include "definitions.h"
#include "ble.h"
#include "functions.h"
#include "state_machine.h"
#include "ble_processing.h"
#include "display.h"
#include "interbot_comm.h"
#include "serial_status.h"

void setup() {
    Serial.begin(UART_SPEED);
    serial_status_init();
    
    // Init display
    display_init();

    module_init_gpios();

    stm_init();   

    ble_start_server();
    interbot_comm_init();

}

void loop() {

    ble_msg_processing();

    interbot_comm_update();

    stm_update();

    check_disconnect_button();

    check_penalty_button();

}
