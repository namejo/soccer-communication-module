"""High-level Python wrapper for the RoboCupJunior Soccer Communication Module."""

from __future__ import annotations

from dataclasses import dataclass
import inspect
import threading
import time
from typing import Any, Callable

from sibcp import (
    BALL_IN_VISION_SERVICE,
    BALL_IN_VISION_SERVICE_ID,
    DefinitionError,
    GameState,
    LedMode,
    MelodyId,
    REQUEST_ROLE_SERVICE,
    REQUEST_ROLE_SERVICE_ID,
    ROBOT_POSE_TOPIC,
    ROBOT_POSE_TOPIC_ID,
    RobotPose,
    RoleResponse,
    SerialTransport,
    ServiceCallError,
    ServiceTimeoutError,
    SibcpNode,
    SYSTEM_GAME_STATE_TOPIC,
    SYSTEM_GAME_STATE_TOPIC_ID,
    SYSTEM_MATCH_TIME_TOPIC,
    SYSTEM_MATCH_TIME_TOPIC_ID,
    SYSTEM_PLAY_MELODY_SERVICE,
    SYSTEM_PLAY_MELODY_SERVICE_ID,
    SYSTEM_REFEREE_EVENT_TOPIC,
    SYSTEM_REFEREE_EVENT_TOPIC_ID,
    SYSTEM_SCORE_TOPIC,
    SYSTEM_SCORE_TOPIC_ID,
    SYSTEM_SET_LED_SERVICE,
    SYSTEM_SET_LED_SERVICE_ID,
    TacticalRole,
    SystemGameState,
    SystemMatchTime,
    SystemPlayMelodyRequest,
    SystemRefereeEvent,
    SystemScore,
    SystemSetLedRequest,
    WORLD_BALL_TOPIC,
    WORLD_BALL_TOPIC_ID,
    WORLD_OPPONENT_TOPIC,
    WORLD_OPPONENT_TOPIC_ID,
    WorldBall,
    WorldOpponent,
)

from .schema import SchemaSpec, codec_from_schema

DEFAULT_BAUDRATE = 460800
DEFAULT_RESPONSE_TIMEOUT = 0.5
DEFAULT_ADVERTISE_INTERVAL = 0.5


@dataclass(frozen=True)
class Message:
    """A high-level topic definition."""

    name: str
    path: str
    message_id: int


@dataclass(frozen=True)
class Service:
    """A high-level service definition."""

    name: str
    path: str
    service_id: int


class PeriodicPublisher:
    """Background publisher for one topic at a fixed rate."""

    def __init__(
        self,
        module: "Module",
        name: str,
        provider: Callable[[], Any],
        hz: float,
        *,
        publish_immediately: bool = True,
    ) -> None:
        if hz <= 0:
            raise DefinitionError("publish rate must be greater than 0 Hz")

        self._module = module
        self._name = name
        self._provider = provider
        self._period = 1.0 / hz
        self._publish_immediately = publish_immediately
        self._stop = threading.Event()
        self._thread = threading.Thread(
            target=self._run,
            name=f"rjscm-publisher-{name}",
            daemon=True,
        )

    def start(self) -> "PeriodicPublisher":
        self._thread.start()
        return self

    def stop(self, *, timeout: float = 1.0) -> None:
        self._stop.set()
        self._thread.join(timeout=timeout)

    def _run(self) -> None:
        next_publish = time.monotonic()
        if not self._publish_immediately:
            next_publish += self._period

        while not self._stop.is_set():
            now = time.monotonic()
            wait_time = max(0.0, next_publish - now)
            if self._stop.wait(wait_time):
                return

            try:
                self._module.publish(self._name, self._provider())
            except Exception as exc:  # pragma: no cover - application callback path
                self._module._log(f"periodic publisher {self._name!r} failed: {exc}")

            next_publish += self._period
            if next_publish < time.monotonic():
                next_publish = time.monotonic() + self._period


class Module:
    """Beginner-friendly API for USB-C or UART module communication.

    Use `rjscm.connect("/dev/ttyACM0", robot_id=1)` for the normal USB-C serial
    path on a Raspberry Pi.
    """

    def __init__(
        self,
        transport: Any,
        *,
        robot_id: int = 0,
        response_timeout: float = DEFAULT_RESPONSE_TIMEOUT,
        logger: Callable[[str], None] | None = None,
        start_reader: bool = True,
        standard_definitions: bool = True,
    ) -> None:
        self.robot_id = robot_id
        self.node = SibcpNode(
            transport,
            response_timeout=response_timeout,
            logger=logger,
        )
        self._messages: dict[str, Message] = {}
        self._services: dict[str, Service] = {}
        self._served_service_ids: set[int] = set()
        self._periodic_publishers: list[PeriodicPublisher] = []
        self._advertise_stop = threading.Event()
        self._advertise_thread: threading.Thread | None = None

        if standard_definitions:
            self.define_standard_messages()
            self.define_standard_services()
            self.define_system_messages()
            self.define_system_services()
        if start_reader:
            self.start()

    @classmethod
    def from_serial(
        cls,
        port: str = "/dev/ttyACM0",
        *,
        robot_id: int = 0,
        baudrate: int = DEFAULT_BAUDRATE,
        response_timeout: float = DEFAULT_RESPONSE_TIMEOUT,
        logger: Callable[[str], None] | None = None,
        start_reader: bool = True,
        standard_definitions: bool = True,
        **serial_kwargs: Any,
    ) -> "Module":
        """Open a communication module over USB-C or UART serial."""

        return cls(
            SerialTransport(port, baudrate=baudrate, **serial_kwargs),
            robot_id=robot_id,
            response_timeout=response_timeout,
            logger=logger,
            start_reader=start_reader,
            standard_definitions=standard_definitions,
        )

    @classmethod
    def from_transport(
        cls,
        transport: Any,
        *,
        robot_id: int = 0,
        response_timeout: float = DEFAULT_RESPONSE_TIMEOUT,
        logger: Callable[[str], None] | None = None,
        start_reader: bool = True,
        standard_definitions: bool = True,
    ) -> "Module":
        """Build a module wrapper around a test or custom byte transport."""

        return cls(
            transport,
            robot_id=robot_id,
            response_timeout=response_timeout,
            logger=logger,
            start_reader=start_reader,
            standard_definitions=standard_definitions,
        )

    def start(self) -> None:
        """Start receiving messages and service responses in the background."""

        self.node.start_background_reader()

    def close(self) -> None:
        """Stop background work and close the serial transport."""

        self.stop_periodic_publishers()
        self.stop_advertising()
        self.node.close()

    def __enter__(self) -> "Module":
        return self

    def __exit__(self, _exc_type: Any, _exc: Any, _traceback: Any) -> None:
        self.close()

    def define_message(
        self,
        name: str,
        *,
        id: int,
        schema: SchemaSpec = None,
        path: str | None = None,
    ) -> Message:
        """Define a one-way message that robots can publish and listen to."""

        message_path = _path_from_name(name, path)
        existing = self._messages.get(message_path)
        if existing is not None:
            self._remember_message(name, existing)
            return existing

        self.node.topic(message_path, topic_id=id, payload=codec_from_schema(schema))
        message = Message(name=name, path=message_path, message_id=id)
        self._remember_message(name, message)
        return message

    def on_message(
        self,
        name: str,
        callback: Callable[[Any], None] | None = None,
    ) -> Callable[[Callable[[Any], None]], Callable[[Any], None]] | Callable[[Any], None]:
        """Run a callback every time a topic message arrives."""

        message = self._require_message(name)
        return self.node.on_topic(message.path, callback)

    def subscribe(
        self,
        name: str,
        callback: Callable[[Any], None] | None = None,
    ) -> Callable[[Callable[[Any], None]], Callable[[Any], None]] | Callable[[Any], None]:
        """Alias for `on_message`, using topic language."""

        return self.on_message(name, callback)

    def publish(self, name: str, value: Any = None) -> None:
        """Send a one-way topic message to the other module."""

        message = self._require_message(name)
        self.node.publish(message.path, value)

    def publish_at_hz(
        self,
        name: str,
        provider: Callable[[], Any],
        *,
        hz: float,
        publish_immediately: bool = True,
    ) -> PeriodicPublisher:
        """Publish a topic at a fixed rate.

        The provider is called every period and should return the latest topic
        value. Topics do not wait for subscribers and do not report whether
        another robot consumed the value.
        """

        self._require_message(name)
        publisher = PeriodicPublisher(
            self,
            name,
            provider,
            hz,
            publish_immediately=publish_immediately,
        ).start()
        self._periodic_publishers.append(publisher)
        return publisher

    def expose_topic(
        self,
        name: str,
        *,
        id: int,
        schema: SchemaSpec,
        provider: Callable[[], Any],
        hz: float,
        path: str | None = None,
        publish_immediately: bool = True,
    ) -> PeriodicPublisher:
        """Define a topic and publish its current value at a fixed rate."""

        self.define_message(name, id=id, schema=schema, path=path)
        return self.publish_at_hz(
            name,
            provider,
            hz=hz,
            publish_immediately=publish_immediately,
        )

    def stop_periodic_publishers(self) -> None:
        """Stop all background topic publishers owned by this module."""

        for publisher in list(self._periodic_publishers):
            publisher.stop()
        self._periodic_publishers.clear()

    def define_service(
        self,
        name: str,
        *,
        id: int,
        request: SchemaSpec = None,
        response: SchemaSpec = None,
        path: str | None = None,
    ) -> Service:
        """Define a request/response service."""

        service_path = _path_from_name(name, path)
        existing = self._services.get(service_path)
        if existing is not None:
            self._remember_service(name, existing)
            return existing

        self.node.service(
            service_path,
            service_id=id,
            request=codec_from_schema(request),
            response=codec_from_schema(response),
        )
        service = Service(name=name, path=service_path, service_id=id)
        self._remember_service(name, service)
        return service

    def serve(
        self,
        name: str,
        handler: Callable[..., Any],
        *,
        advertise: bool = True,
        advertise_interval: float = DEFAULT_ADVERTISE_INTERVAL,
    ) -> None:
        """Expose a service from this robot.

        The handler can accept the request value, or accept no arguments for an
        empty-request service.
        """

        service = self._require_service(name)
        self.node.on_service(
            service.path,
            lambda request: _call_service_handler(handler, request),
        )
        self._served_service_ids.add(service.service_id)
        if advertise:
            self.start_advertising(interval=advertise_interval)

    def call_service(
        self,
        name: str,
        request: Any = None,
        *,
        peer_id: int | None = None,
        timeout: float | None = None,
        attempts: int = 3,
        require_advertisement: bool = True,
        discovery_timeout: float = 2.0,
    ) -> Any:
        """Call a service on the peer robot.

        By default this waits until the peer advertises the service, which keeps
        beginner code from silently calling services that are not available yet.
        """

        service = self._require_service(name)
        if require_advertisement:
            self.wait_for_service(name, peer_id=peer_id, timeout=discovery_timeout)

        last_error: BaseException | None = None
        for _attempt in range(max(1, attempts)):
            try:
                return self.node.call(service.path, request, timeout=timeout)
            except ServiceTimeoutError as exc:
                last_error = exc
        if last_error is not None:
            raise last_error
        raise ServiceCallError(1, f"service {service.path} failed")

    def wait_for_service(
        self,
        name: str,
        *,
        peer_id: int | None = None,
        timeout: float = 2.0,
    ) -> None:
        """Wait until a peer advertises a service."""

        service = self._require_service(name)
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if self._service_is_discovered(service, peer_id):
                return
            time.sleep(0.01)
        peer = "any peer" if peer_id is None else f"robot {peer_id}"
        raise ServiceTimeoutError(f"{peer} did not advertise {service.path}")

    def advertise_once(self) -> None:
        """Tell peers which services this robot currently provides."""

        self.node.advertise_services(
            source_robot_id=self.robot_id,
            service_ids=self._served_service_ids,
        )

    def start_advertising(
        self,
        *,
        interval: float = DEFAULT_ADVERTISE_INTERVAL,
    ) -> None:
        """Continuously advertise provided services for peer discovery."""

        if self._advertise_thread is not None and self._advertise_thread.is_alive():
            return

        self._advertise_stop.clear()

        def advertise_loop() -> None:
            while not self._advertise_stop.is_set():
                self.advertise_once()
                self._advertise_stop.wait(interval)

        self._advertise_thread = threading.Thread(
            target=advertise_loop,
            name="rjscm-advertise",
            daemon=True,
        )
        self._advertise_thread.start()

    def stop_advertising(self, *, timeout: float = 1.0) -> None:
        """Stop periodic service advertisements."""

        self._advertise_stop.set()
        if self._advertise_thread is not None:
            self._advertise_thread.join(timeout=timeout)

    def define_standard_messages(self) -> None:
        """Register common robot/world messages."""

        self._define_message_aliases(
            ("robot_pose", ROBOT_POSE_TOPIC),
            id=ROBOT_POSE_TOPIC_ID,
            schema=RobotPose,
        )
        self._define_message_aliases(
            ("world_ball", "ball", WORLD_BALL_TOPIC),
            id=WORLD_BALL_TOPIC_ID,
            schema=WorldBall,
        )
        self._define_message_aliases(
            ("world_opponent", "opponent", WORLD_OPPONENT_TOPIC),
            id=WORLD_OPPONENT_TOPIC_ID,
            schema=WorldOpponent,
        )

    def define_standard_services(self) -> None:
        """Register common robot-to-robot services."""

        self._define_service_aliases(
            ("see_ball", "ball_in_your_vision", BALL_IN_VISION_SERVICE),
            id=BALL_IN_VISION_SERVICE_ID,
            response=bool,
        )
        self._define_service_aliases(
            ("request_role", REQUEST_ROLE_SERVICE),
            id=REQUEST_ROLE_SERVICE_ID,
            response=RoleResponse,
        )

    def define_system_messages(self) -> None:
        """Register firmware-owned system topics such as game state and score."""

        self._define_message_aliases(
            ("game_state", SYSTEM_GAME_STATE_TOPIC),
            id=SYSTEM_GAME_STATE_TOPIC_ID,
            schema=SystemGameState,
        )
        self._define_message_aliases(
            ("score", SYSTEM_SCORE_TOPIC),
            id=SYSTEM_SCORE_TOPIC_ID,
            schema=SystemScore,
        )
        self._define_message_aliases(
            ("match_time", SYSTEM_MATCH_TIME_TOPIC),
            id=SYSTEM_MATCH_TIME_TOPIC_ID,
            schema=SystemMatchTime,
        )
        self._define_message_aliases(
            ("referee_event", SYSTEM_REFEREE_EVENT_TOPIC),
            id=SYSTEM_REFEREE_EVENT_TOPIC_ID,
            schema=SystemRefereeEvent,
        )

    def define_system_services(self) -> None:
        """Register firmware-owned local services such as LED and melody control."""

        self._define_service_aliases(
            ("set_led", SYSTEM_SET_LED_SERVICE),
            id=SYSTEM_SET_LED_SERVICE_ID,
            request=SystemSetLedRequest,
        )
        self._define_service_aliases(
            ("play_melody", SYSTEM_PLAY_MELODY_SERVICE),
            id=SYSTEM_PLAY_MELODY_SERVICE_ID,
            request=SystemPlayMelodyRequest,
        )

    def answer_see_ball(
        self,
        handler_or_value: Callable[[], bool] | bool,
        *,
        advertise: bool = True,
    ) -> None:
        """Expose the standard `see_ball` service."""

        def handler(_request: Any) -> bool:
            if callable(handler_or_value):
                return bool(handler_or_value())
            return bool(handler_or_value)

        self.serve("see_ball", handler, advertise=advertise)

    def ask_peer_sees_ball(
        self,
        *,
        peer_id: int | None = None,
        timeout: float | None = None,
        attempts: int = 3,
        discovery_timeout: float = 2.0,
    ) -> bool:
        """Ask another robot whether it sees the ball."""

        return bool(
            self.call_service(
                "see_ball",
                peer_id=peer_id,
                timeout=timeout,
                attempts=attempts,
                discovery_timeout=discovery_timeout,
            )
        )

    def answer_role(
        self,
        role: Callable[[], TacticalRole | int] | TacticalRole | int,
        *,
        advertise: bool = True,
    ) -> None:
        """Expose this robot's current tactical role."""

        def handler(_request: Any) -> dict[str, int]:
            current = role() if callable(role) else role
            return {"role": int(current)}

        self.serve("request_role", handler, advertise=advertise)

    def ask_peer_role(
        self,
        *,
        peer_id: int | None = None,
        timeout: float | None = None,
        attempts: int = 3,
        discovery_timeout: float = 2.0,
    ) -> TacticalRole:
        """Ask another robot for its tactical role."""

        response = self.call_service(
            "request_role",
            peer_id=peer_id,
            timeout=timeout,
            attempts=attempts,
            discovery_timeout=discovery_timeout,
        )
        return TacticalRole(response["role"])

    def publish_ball(
        self,
        *,
        visible: bool,
        x_mm: int = 0,
        y_mm: int = 0,
        confidence: int = 100,
        timestamp_ms: int | None = None,
    ) -> None:
        """Publish the standard ball-position topic."""

        self.publish(
            "ball",
            {
                "visible": visible,
                "x_mm": x_mm,
                "y_mm": y_mm,
                "confidence": confidence,
                "timestamp_ms": _timestamp_ms(timestamp_ms),
            },
        )

    def publish_pose(
        self,
        *,
        x_mm: int,
        y_mm: int,
        heading_mrad: int,
        confidence: int = 100,
        timestamp_ms: int | None = None,
    ) -> None:
        """Publish this robot's current pose."""

        self.publish(
            "robot_pose",
            {
                "x_mm": x_mm,
                "y_mm": y_mm,
                "heading_mrad": heading_mrad,
                "confidence": confidence,
                "timestamp_ms": _timestamp_ms(timestamp_ms),
            },
        )

    def on_game_state(
        self,
        callback: Callable[[GameState, bool], None],
    ) -> Callable[[Any], None]:
        """Receive referee play/stop state as `GameState` plus `robot_play`."""

        def handler(message: dict[str, Any]) -> None:
            callback(GameState(message["state"]), bool(message["robot_play"]))

        self.on_message("game_state", handler)
        return handler

    def set_led(
        self,
        *,
        red: int,
        green: int,
        blue: int,
        mode: LedMode | int = LedMode.SOLID,
        duration_ms: int = 0,
        timeout: float | None = None,
    ) -> None:
        """Ask the local module firmware to set its status LED."""

        self.call_service(
            "set_led",
            {
                "mode": int(mode),
                "red": red,
                "green": green,
                "blue": blue,
                "duration_ms": duration_ms,
            },
            timeout=timeout,
            require_advertisement=False,
            attempts=1,
        )

    def play_melody(
        self,
        melody: MelodyId | int = MelodyId.GOAL,
        *,
        repeat: int = 1,
        timeout: float | None = None,
    ) -> None:
        """Ask the local module firmware to play a built-in melody."""

        self.call_service(
            "play_melody",
            {"melody_id": int(melody), "repeat": repeat},
            timeout=timeout,
            require_advertisement=False,
            attempts=1,
        )

    def _define_message_aliases(
        self,
        aliases: tuple[str, ...],
        *,
        id: int,
        schema: SchemaSpec,
    ) -> None:
        message = self.define_message(
            aliases[0],
            id=id,
            schema=schema,
            path=_path_alias(aliases),
        )
        for alias in aliases[1:]:
            self._remember_message(alias, message)

    def _define_service_aliases(
        self,
        aliases: tuple[str, ...],
        *,
        id: int,
        request: SchemaSpec = None,
        response: SchemaSpec = None,
    ) -> None:
        service = self.define_service(
            aliases[0],
            id=id,
            request=request,
            response=response,
            path=_path_alias(aliases),
        )
        for alias in aliases[1:]:
            self._remember_service(alias, service)

    def _remember_message(self, name: str, message: Message) -> None:
        self._messages[name] = message
        self._messages[message.path] = message
        self._messages[_short_name(message.path)] = message

    def _remember_service(self, name: str, service: Service) -> None:
        self._services[name] = service
        self._services[service.path] = service
        self._services[_short_name(service.path)] = service

    def _require_message(self, name: str) -> Message:
        try:
            return self._messages[name]
        except KeyError as exc:
            raise DefinitionError(f"unknown message {name!r}") from exc

    def _require_service(self, name: str) -> Service:
        try:
            return self._services[name]
        except KeyError as exc:
            raise DefinitionError(f"unknown service {name!r}") from exc

    def _service_is_discovered(self, service: Service, peer_id: int | None) -> bool:
        if peer_id is not None:
            return service.service_id in self.node.discovered_services.get(peer_id, set())
        return any(
            service.service_id in services
            for services in self.node.discovered_services.values()
        )

    def _log(self, message: str) -> None:
        if self.node.logger is not None:
            self.node.logger(message)


def connect(
    port: str = "/dev/ttyACM0",
    *,
    robot_id: int = 0,
    baudrate: int = DEFAULT_BAUDRATE,
    response_timeout: float = DEFAULT_RESPONSE_TIMEOUT,
    logger: Callable[[str], None] | None = None,
    start_reader: bool = True,
    **serial_kwargs: Any,
) -> Module:
    """Connect to a module over USB-C serial and return the high-level wrapper."""

    return Module.from_serial(
        port,
        robot_id=robot_id,
        baudrate=baudrate,
        response_timeout=response_timeout,
        logger=logger,
        start_reader=start_reader,
        **serial_kwargs,
    )


def _path_from_name(name: str, path: str | None) -> str:
    if path is not None:
        return _normalize_path(path)
    return _normalize_path(name)


def _normalize_path(value: str) -> str:
    if not isinstance(value, str) or not value.strip():
        raise DefinitionError("message and service names must not be empty")
    cleaned = value.strip()
    if cleaned.startswith("/"):
        return cleaned
    return "/" + cleaned.strip("/")


def _path_alias(aliases: tuple[str, ...]) -> str:
    for alias in aliases:
        if alias.startswith("/"):
            return alias
    return _normalize_path(aliases[0])


def _short_name(path: str) -> str:
    return path.strip("/").replace("/", "_")


def _timestamp_ms(value: int | None) -> int:
    if value is not None:
        return value
    return int(time.monotonic() * 1000) & 0xFFFFFFFF


def _call_service_handler(handler: Callable[..., Any], request: Any) -> Any:
    try:
        signature = inspect.signature(handler)
    except (TypeError, ValueError):
        return handler(request)

    parameters = list(signature.parameters.values())
    has_varargs = any(item.kind == item.VAR_POSITIONAL for item in parameters)
    positional = [
        item
        for item in parameters
        if item.kind in (item.POSITIONAL_ONLY, item.POSITIONAL_OR_KEYWORD)
    ]
    required_positional = [
        item for item in positional if item.default is item.empty
    ]
    if has_varargs or required_positional:
        return handler(request)
    return handler()
