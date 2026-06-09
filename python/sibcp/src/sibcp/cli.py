"""Command line helpers for trying SIBCP services on real modules."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import sys
import time
from typing import Any, Callable

from .codecs import Bool, Codec, Empty
from .node import ServiceCallError, ServiceTimeoutError, SibcpNode
from .schemas import (
    BALL_IN_VISION_SERVICE,
    BALL_IN_VISION_SERVICE_ID,
    REQUEST_ROLE_SERVICE,
    REQUEST_ROLE_SERVICE_ID,
    RoleResponse,
    TacticalRole,
)
from .transport import SerialTransport


@dataclass(frozen=True)
class CliService:
    name: str
    aliases: tuple[str, ...]
    path: str
    service_id: int
    response: Codec
    default_value: str
    parse_value: Callable[[str], Any]
    format_value: Callable[[Any], str]

    def register(self, node: SibcpNode) -> None:
        node.service(
            self.path,
            service_id=self.service_id,
            request=Empty,
            response=self.response,
        )


def _parse_bool(value: str) -> bool:
    normalized = value.strip().lower()
    if normalized in {"1", "true", "yes", "y", "on"}:
        return True
    if normalized in {"0", "false", "no", "n", "off"}:
        return False
    raise argparse.ArgumentTypeError(
        "expected a boolean value like true/false, yes/no, or 1/0"
    )


def _parse_role(value: str) -> dict[str, int]:
    normalized = value.strip().replace("-", "_").upper()
    try:
        role = TacticalRole[normalized]
    except KeyError:
        try:
            role = TacticalRole(int(value, 0))
        except (ValueError, KeyError) as exc:
            names = ", ".join(role.name.lower() for role in TacticalRole)
            raise argparse.ArgumentTypeError(f"expected one of: {names}") from exc
    return {"role": int(role)}


def _format_bool(value: Any) -> str:
    return "true" if bool(value) else "false"


def _format_role(value: Any) -> str:
    if not isinstance(value, dict) or "role" not in value:
        return repr(value)
    role_id = int(value["role"])
    try:
        return TacticalRole(role_id).name.lower()
    except ValueError:
        return str(role_id)


SERVICES = (
    CliService(
        name="see_ball",
        aliases=(
            "see_ball",
            "sees_ball",
            "ball",
            "ball_in_vision",
            "ball_in_your_vision",
            "/ball_in_your_vision",
        ),
        path=BALL_IN_VISION_SERVICE,
        service_id=BALL_IN_VISION_SERVICE_ID,
        response=Bool,
        default_value="false",
        parse_value=_parse_bool,
        format_value=_format_bool,
    ),
    CliService(
        name="request_role",
        aliases=("request_role", "role", "/request_role"),
        path=REQUEST_ROLE_SERVICE,
        service_id=REQUEST_ROLE_SERVICE_ID,
        response=RoleResponse,
        default_value="unknown",
        parse_value=_parse_role,
        format_value=_format_role,
    ),
)


def _service_names() -> str:
    return ", ".join(service.name for service in SERVICES)


def _find_service(name: str) -> CliService:
    normalized = name.strip().lower()
    for service in SERVICES:
        if normalized in service.aliases:
            return service
    raise argparse.ArgumentTypeError(
        f"unknown service {name!r}; available services: {_service_names()}"
    )


def _wait_for_advertised_service(
    node: SibcpNode,
    *,
    service_id: int,
    peer_id: int | None,
    timeout: float,
) -> int | None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if peer_id is not None:
            if service_id in node.discovered_services.get(peer_id, set()):
                return peer_id
        else:
            for discovered_peer, service_ids in sorted(node.discovered_services.items()):
                if service_id in service_ids:
                    return discovered_peer
        time.sleep(0.02)
    return None


def _serve_service(args: argparse.Namespace) -> int:
    service: CliService = args.service
    response_value = service.parse_value(args.value)
    node = SibcpNode(
        SerialTransport(args.port, baudrate=args.baudrate),
        response_timeout=args.timeout,
        logger=lambda message: print(f"node: {message}", file=sys.stderr),
    )
    service.register(node)

    @node.on_service(service.path)
    def handle_request(_request: None) -> Any:
        print(
            f"received {service.name} request -> sending "
            f"{service.format_value(response_value)}",
            flush=True,
        )
        return response_value

    node.start_background_reader()
    print(
        f"serving {service.name} as robot {args.robot_id} on {args.port}; "
        "press Ctrl+C to stop",
        flush=True,
    )
    try:
        while True:
            node.advertise_services(source_robot_id=args.robot_id)
            time.sleep(args.advertise_interval)
    except KeyboardInterrupt:
        print("stopped")
        return 0
    finally:
        node.close()


def _call_service(args: argparse.Namespace) -> int:
    service: CliService = args.service
    node = SibcpNode(
        SerialTransport(args.port, baudrate=args.baudrate),
        response_timeout=args.timeout,
        logger=lambda message: print(f"node: {message}", file=sys.stderr),
    )
    service.register(node)
    node.start_background_reader()

    try:
        print(
            f"waiting for {service.name} advertisement"
            + (f" from robot {args.peer_id}" if args.peer_id is not None else "")
            + "...",
            flush=True,
        )
        discovered_peer = _wait_for_advertised_service(
            node,
            service_id=service.service_id,
            peer_id=args.peer_id,
            timeout=args.discovery_timeout,
        )
        if discovered_peer is None:
            print(
                f"no peer advertised {service.name} within "
                f"{args.discovery_timeout:.1f}s",
                file=sys.stderr,
            )
            return 2

        print(f"found {service.name} on robot {discovered_peer}", flush=True)
        last_error: BaseException | None = None
        for attempt in range(1, args.attempts + 1):
            try:
                response = node.call(service.path, timeout=args.timeout)
            except (ServiceTimeoutError, ServiceCallError) as exc:
                last_error = exc
                print(f"attempt {attempt} failed: {exc}", file=sys.stderr)
                continue

            print(f"{service.name} response: {service.format_value(response)}")
            return 0

        print(
            f"{service.name} failed after {args.attempts} attempt(s): {last_error}",
            file=sys.stderr,
        )
        return 1
    finally:
        node.close()


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="sibcp",
        description="Try SIBCP robot-to-robot services over a communication module.",
    )
    commands = parser.add_subparsers(dest="command", required=True)

    service_parser = commands.add_parser("service", help="serve or call a service")
    service_commands = service_parser.add_subparsers(
        dest="service_command",
        required=True,
    )

    serve = service_commands.add_parser(
        "serve",
        help="expose a service and answer peer requests",
    )
    serve.add_argument("service", type=_find_service, help=f"service name: {_service_names()}")
    serve.add_argument("--port", default="/dev/ttyACM0", help="serial port for this module")
    serve.add_argument("--baudrate", type=int, default=460800)
    serve.add_argument("--robot-id", type=int, default=1, help="id advertised to peers")
    serve.add_argument(
        "--value",
        default=None,
        help="response value, for example true/false for see_ball",
    )
    serve.add_argument("--timeout", type=float, default=1.0)
    serve.add_argument("--advertise-interval", type=float, default=0.5)
    serve.set_defaults(func=_serve_service)

    call = service_commands.add_parser(
        "call",
        help="wait for a peer advertisement, then call a service",
    )
    call.add_argument("service", type=_find_service, help=f"service name: {_service_names()}")
    call.add_argument("--port", default="/dev/ttyACM0", help="serial port for this module")
    call.add_argument("--baudrate", type=int, default=460800)
    call.add_argument("--peer-id", type=int, help="only accept this advertised robot id")
    call.add_argument("--discovery-timeout", type=float, default=5.0)
    call.add_argument("--timeout", type=float, default=1.0, help="timeout per call attempt")
    call.add_argument("--attempts", type=int, default=3, help="service call attempts")
    call.set_defaults(func=_call_service)

    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    if getattr(args, "value", None) is None and hasattr(args, "service"):
        args.value = args.service.default_value
    try:
        return int(args.func(args))
    except RuntimeError as exc:
        print(str(exc), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
