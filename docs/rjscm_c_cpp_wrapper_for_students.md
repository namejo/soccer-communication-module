# RJSCM C And C++ Wrapper For Students

Use this if your robot code is written in C or C++ and you want to talk to the
communication module over USB-C.

The wireless bridge is currently best-effort ESP-NOW broadcast. Use it for
tactics and sensor sharing, but do not treat it as encrypted, authenticated, or
guaranteed delivery.

Include one header:

```c
#include "rjscm.h"
```

On Raspberry Pi, the USB-C module is usually `/dev/ttyACM0`.

## Build The Library

```sh
cmake -S c/rjscm -B /tmp/rjscm-build
cmake --build /tmp/rjscm-build
```

For a quick local install:

```sh
cmake --install /tmp/rjscm-build --prefix /tmp/rjscm-install
```

Then compile a small C robot program like this:

```sh
gcc my_robot.c -I/tmp/rjscm-install/include -L/tmp/rjscm-install/lib -lrjscm -o my_robot
```

Or use it from CMake:

```cmake
find_package(rjscm CONFIG REQUIRED)
add_executable(my_robot my_robot.cpp)
target_link_libraries(my_robot PRIVATE rjscm::rjscm)
```

If you do not install it, your program must include `c/rjscm/include` and link
against the built `librjscm.a`.

## Most Important Rule

Your robot must call `update` often:

```c
rjscm_update(robot, 10);
```

or:

```cpp
robot.update(10);
```

That is how your program receives messages, answers service requests, and
advertises services to the other robot.

## C Example: Answer `see_ball`

```c
#include "rjscm.h"

static bool see_ball(void *user_data) {
    (void)user_data;
    return camera_sees_ball();
}

int main(void) {
    rjscm_module_t *robot = NULL;
    rjscm_open_usb(&robot, "/dev/ttyACM0", 2);

    rjscm_answer_see_ball(robot, see_ball, NULL);

    while (true) {
        rjscm_update(robot, 10);
        robot_loop();
    }
}
```

## C Example: Ask `see_ball`

```c
rjscm_module_t *robot = NULL;
rjscm_open_usb(&robot, "/dev/ttyACM0", 1);

bool sees_ball = false;
rjscm_ask_peer_sees_ball(robot, 2, 500, &sees_ball);
```

## C++ Example

```cpp
#include "rjscm.h"

int main() {
    auto robot = rjscm::Module::open("/dev/ttyACM0", 1);

    robot.answerSeeBall([] {
        return cameraSeesBall();
    });

    while (true) {
        robot.update(10);
        robotLoop();
    }
}
```

## Custom C Message

```c
typedef struct RJSCM_PACKED my_location {
    int32_t x_mm;
    int32_t y_mm;
} my_location_t;

RJSCM_DEFINE_MESSAGE_TYPE(robot, "my_location", 0x30, my_location_t);

my_location_t location = {.x_mm = 1200, .y_mm = -400};
RJSCM_PUBLISH_VALUE(robot, "my_location", &location);
```

`RJSCM_PACKED` is important for custom structs because the protocol sends fixed
binary bytes. Without it, a compiler can insert padding bytes between fields.

## Periodic C Topic

Use this for sensor data that should be sent many times per second. It is still
a topic, so there is no request and no response.

```c
static rjscm_result_t provide_distance(
    void *payload,
    size_t capacity,
    size_t *length,
    void *user_data
) {
    (void)user_data;
    if (capacity < sizeof(int32_t)) {
        return RJSCM_INVALID_ARGUMENT;
    }
    int32_t distance_mm = distance_sensor_read_mm();
    memcpy(payload, &distance_mm, sizeof(distance_mm));
    *length = sizeof(distance_mm);
    return RJSCM_OK;
}

RJSCM_DEFINE_MESSAGE_TYPE(robot, "front_distance", 0x32, int32_t);
rjscm_publish_at_hz(robot, "front_distance", 20.0, provide_distance, NULL);
```

## Custom C Service

Use a service when the other robot should ask a question and wait for a response.
This example answers whether this robot can shoot.

```c
static rjscm_result_t can_shoot_handler(
    const void *request,
    size_t request_length,
    void *response,
    size_t response_capacity,
    size_t *response_length,
    void *user_data
) {
    (void)request;
    (void)user_data;
    if (request_length != 0 || response_capacity < sizeof(uint8_t)) {
        return RJSCM_INVALID_ARGUMENT;
    }

    uint8_t can_shoot = robot_has_clear_shot() ? 1 : 0;
    memcpy(response, &can_shoot, sizeof(can_shoot));
    *response_length = sizeof(can_shoot);
    return RJSCM_OK;
}

rjscm_define_service(robot, "can_shoot", 0x33, 0, sizeof(uint8_t));
rjscm_serve(robot, "can_shoot", can_shoot_handler, NULL);
```

The other robot calls it like this:

```c
uint8_t can_shoot = 0;
size_t response_length = 0;
rjscm_call_service(
    robot,
    "can_shoot",
    NULL,
    0,
    &can_shoot,
    sizeof(can_shoot),
    &response_length,
    2,
    500
);
```

## Custom C++ Message

```cpp
struct Location {
    int32_t x_mm;
    int32_t y_mm;
};

robot.defineMessage<Location>("my_location", 0x30);
robot.publish("my_location", Location{1200, -400});
```

## Periodic C++ Topic

```cpp
robot.defineMessage<int32_t>("front_distance", 0x32);
robot.publishAtHz<int32_t>("front_distance", 20.0, [] {
    return distanceSensorReadMm();
});
```

## ID Rules

- Both robots must use the same ID and struct layout.
- Use IDs `0x20` through `0xEF` for team messages and services.
- IDs `0xF0` through `0xFF` are reserved for system features.
- Do not reuse the same custom ID for two different names. The wrapper rejects
  duplicate custom topic or service IDs.

The C/C++ wrapper sends structs as fixed-size binary data. Raspberry Pi and
ESP32 use little-endian numbers, so this matches the intended hardware setup.
For a fuller list of recommended field types, see
[RJSCM datatypes](rjscm_datatypes.md).

More setup help:

- [Raspberry Pi USB-C setup](raspberry_pi_setup.md)
- [RJSCM datatypes](rjscm_datatypes.md)
- [SIBCP ID allocation](sibcp_id_allocation.md)
- [Two-module hardware checklist](two_module_hardware_checklist.md)
