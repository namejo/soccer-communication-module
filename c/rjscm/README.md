# RJSCM C/C++ Wrapper

This package is the C and C++ version of the high-level RJSCM client. It talks
to the local communication module over USB-C serial, usually `/dev/ttyACM0` on a
Raspberry Pi.

The C API is available with:

```c
#include "rjscm.h"
```

The same header also exposes a C++ wrapper when included from C++:

```cpp
#include "rjscm.h"
```

## Build

From the repository root:

```sh
cmake -S c/rjscm -B /tmp/rjscm-build
cmake --build /tmp/rjscm-build
ctest --test-dir /tmp/rjscm-build --output-on-failure
```

Optional local install:

```sh
cmake --install /tmp/rjscm-build --prefix /tmp/rjscm-install
```

Then compile a small C program with:

```sh
gcc my_robot.c -I/tmp/rjscm-install/include -L/tmp/rjscm-install/lib -lrjscm -o my_robot
```

Or use it from CMake:

```cmake
find_package(rjscm CONFIG REQUIRED)
add_executable(my_robot my_robot.cpp)
target_link_libraries(my_robot PRIVATE rjscm::rjscm)
```

## C: Answer Whether This Robot Sees The Ball

```c
#include "rjscm.h"

static bool see_ball(void *user_data) {
    (void)user_data;
    return camera_sees_ball();
}

int main(void) {
    rjscm_module_t *robot = NULL;
    if (rjscm_open_usb(&robot, "/dev/ttyACM0", 2) != RJSCM_OK) {
        return 1;
    }

    rjscm_answer_see_ball(robot, see_ball, NULL);

    while (true) {
        rjscm_update(robot, 10);
        robot_loop();
    }
}
```

## C: Ask The Other Robot

```c
#include "rjscm.h"

int main(void) {
    rjscm_module_t *robot = NULL;
    rjscm_open_usb(&robot, "/dev/ttyACM0", 1);

    bool peer_sees_ball = false;
    if (rjscm_ask_peer_sees_ball(robot, 2, 500, &peer_sees_ball) == RJSCM_OK) {
        if (peer_sees_ball) {
            /* Adapt your tactic here. */
        }
    }

    rjscm_close(robot);
}
```

## C: Custom Message

Both robots must define the same message ID and struct layout:

```c
typedef struct RJSCM_PACKED my_location {
    int32_t x_mm;
    int32_t y_mm;
} my_location_t;

RJSCM_DEFINE_MESSAGE_TYPE(robot, "my_location", 0x30, my_location_t);
```

Send:

```c
my_location_t location = {.x_mm = 1200, .y_mm = -400};
RJSCM_PUBLISH_VALUE(robot, "my_location", &location);
```

Receive:

```c
static void handle_location(const void *payload, size_t length, void *user_data) {
    if (length != sizeof(my_location_t)) {
        return;
    }
    const my_location_t *location = (const my_location_t *)payload;
    update_peer_location(location->x_mm, location->y_mm);
}

rjscm_on_message(robot, "my_location", handle_location, NULL);
```

Publish a topic at a fixed rate from `rjscm_update()`:

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

Payload structs are sent as fixed-size binary data. Raspberry Pi and ESP32 are
little-endian, so this is fine for the expected robot setup. If you use another
CPU architecture, keep the same field sizes and byte order on both robots. Use
`RJSCM_PACKED` on custom structs so the compiler does not insert padding bytes.

## C: Custom Service

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

## C++: High-Level Wrapper

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

Custom C++ messages:

```cpp
struct Location {
    int32_t x_mm;
    int32_t y_mm;
};

robot.defineMessage<Location>("my_location", 0x30);
robot.publish("my_location", Location{1200, -400});

robot.onMessage<Location>("my_location", [](const Location &location) {
    updatePeerLocation(location.x_mm, location.y_mm);
});
```

Periodic C++ topic:

```cpp
robot.defineMessage<int32_t>("front_distance", 0x32);
robot.publishAtHz<int32_t>("front_distance", 20.0, [] {
    return distanceSensorReadMm();
});
```

## Important Loop Rule

Every robot must regularly call:

```c
rjscm_update(robot, 10);
```

or in C++:

```cpp
robot.update(10);
```

This receives messages, answers service requests, and advertises services to the
other module.

## ID Rules

- Use IDs `0x20` through `0xEF` for team-defined messages and services.
- Use the same ID and struct layout on both robots.
- IDs `0xF0` through `0xFF` are reserved for system messages and services.
- Do not reuse one custom ID for two names. The wrapper rejects duplicate
  custom topic or service IDs.
