#include "rjscm.h"

#include <cstdint>
#include <iostream>
#include <sys/socket.h>

struct CppLocation {
    int32_t x_mm;
    int32_t y_mm;
};

int main() {
    int fds[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) {
        std::cerr << "socketpair failed\n";
        return 1;
    }

    rjscm_module_t *left_raw = nullptr;
    rjscm_module_t *right_raw = nullptr;
    rjscm::check(rjscm_open_fd(&left_raw, fds[0], 1));
    rjscm::check(rjscm_open_fd(&right_raw, fds[1], 2));

    rjscm::Module left(left_raw);
    rjscm::Module right(right_raw);

    left.defineMessage<CppLocation>("cpp_location", 0x40);
    right.defineMessage<CppLocation>("cpp_location", 0x40);

    CppLocation received{0, 0};
    right.onMessage<CppLocation>(
        "cpp_location",
        [&](const CppLocation &message) {
            received = message;
        }
    );

    const CppLocation sent{42, -7};
    left.publish("cpp_location", sent);
    right.update(100);

    if (received.x_mm != sent.x_mm || received.y_mm != sent.y_mm) {
        std::cerr << "C++ message payload mismatch\n";
        return 1;
    }

    CppLocation periodic{100, 5};
    received = CppLocation{0, 0};
    left.publishAtHz<CppLocation>("cpp_location", 50.0, [&] {
        periodic.x_mm += 1;
        return periodic;
    });

    for (int i = 0; i < 20 && received.x_mm < 102; ++i) {
        left.update(10);
        right.update(10);
    }
    left.stopPublishing("cpp_location");

    if (received.x_mm < 102 || received.y_mm != 5) {
        std::cerr << "C++ periodic topic did not publish repeatedly\n";
        return 1;
    }

    return 0;
}
