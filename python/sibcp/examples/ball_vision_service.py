"""Example SIBCP boolean service for asking a peer robot about ball vision."""

from __future__ import annotations

import argparse
import time

from sibcp import Bool, SerialTransport, SibcpNode

BALL_SERVICE = "/ball_in_your_vision"
BALL_SERVICE_ID = 1


def sees_ball() -> bool:
    """Replace this with your camera or vision pipeline."""
    return False


def run_server(port: str) -> None:
    node = SibcpNode(SerialTransport(port, baudrate=460800), logger=print)
    node.service(BALL_SERVICE, service_id=BALL_SERVICE_ID, response=Bool)

    @node.on_service(BALL_SERVICE)
    def handle_ball_request(_request: None) -> bool:
        return sees_ball()

    node.start_background_reader()
    print(f"listening for {BALL_SERVICE} on {port}")
    try:
        while True:
            time.sleep(1.0)
    finally:
        node.close()


def run_client(port: str, interval: float) -> None:
    node = SibcpNode(SerialTransport(port, baudrate=460800), logger=print)
    node.service(BALL_SERVICE, service_id=BALL_SERVICE_ID, response=Bool)
    try:
        while True:
            value = node.call(BALL_SERVICE, timeout=0.2)
            print(f"peer sees ball: {value}")
            time.sleep(interval)
    finally:
        node.close()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("mode", choices=("server", "client"))
    parser.add_argument("--port", default="/dev/ttyAMA0")
    parser.add_argument("--interval", type=float, default=0.25)
    args = parser.parse_args()

    if args.mode == "server":
        run_server(args.port)
    else:
        run_client(args.port, args.interval)


if __name__ == "__main__":
    main()
