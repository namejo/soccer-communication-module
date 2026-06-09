"""High-level SIBCP topic and service node."""

from __future__ import annotations

from collections.abc import Iterable
from dataclasses import dataclass
import threading
import time
from typing import Any, Callable

from .codecs import Codec, CodecError, Empty
from .protocol import Frame, PacketType, StreamingParser, encode_frame, packet_type_name

SERVICE_STATUS_OK = 0x00
SERVICE_STATUS_ERROR = 0x01
SERVICE_STATUS_UNSUPPORTED = 0x02


class DefinitionError(ValueError):
    """Raised when topic or service definitions are inconsistent."""


class ServiceTimeoutError(TimeoutError):
    """Raised when a service response does not arrive before the timeout."""


class ServiceCallError(RuntimeError):
    """Raised when a remote service responds with a nonzero status code."""

    def __init__(self, status_code: int, message: str | None = None) -> None:
        self.status_code = status_code
        super().__init__(message or f"remote service failed with status {status_code}")


@dataclass(frozen=True)
class ServiceResult:
    """Explicit service handler result with a custom status code."""

    status_code: int
    value: Any = None


@dataclass(frozen=True)
class TopicDefinition:
    path: str
    topic_id: int
    payload: Codec


@dataclass(frozen=True)
class ServiceDefinition:
    path: str
    service_id: int
    request: Codec
    response: Codec


@dataclass
class _PendingCall:
    definition: ServiceDefinition
    event: threading.Event
    response: Any = None
    error: BaseException | None = None


class SibcpNode:
    """Small synchronous/asynchronous helper around a SIBCP byte transport."""

    def __init__(
        self,
        transport: Any,
        *,
        response_timeout: float = 0.5,
        logger: Callable[[str], None] | None = None,
    ) -> None:
        self.transport = transport
        self.response_timeout = response_timeout
        self.logger = logger

        self._parser = StreamingParser()
        self._send_lock = threading.Lock()
        self._pending_lock = threading.Lock()
        self._pending: dict[tuple[int, int], _PendingCall] = {}
        self._transaction_id = 0

        self._topics_by_path: dict[str, TopicDefinition] = {}
        self._topics_by_id: dict[int, TopicDefinition] = {}
        self._services_by_path: dict[str, ServiceDefinition] = {}
        self._services_by_id: dict[int, ServiceDefinition] = {}
        self._topic_subscribers: dict[int, list[Callable[[Any], None]]] = {}
        self._service_handlers: dict[int, Callable[[Any], Any]] = {}
        self.discovered_services: dict[int, set[int]] = {}

        self._stop_reader = threading.Event()
        self._reader_thread: threading.Thread | None = None

    def topic(self, path: str, topic_id: int, payload: Codec) -> TopicDefinition:
        _validate_path(path)
        _validate_id(topic_id, "topic_id")
        _validate_codec(payload)
        if path in self._topics_by_path:
            raise DefinitionError(f"topic path {path!r} is already defined")
        if topic_id in self._topics_by_id:
            existing = self._topics_by_id[topic_id]
            raise DefinitionError(
                f"topic id {topic_id} is already used by {existing.path!r}"
            )

        definition = TopicDefinition(path=path, topic_id=topic_id, payload=payload)
        self._topics_by_path[path] = definition
        self._topics_by_id[topic_id] = definition
        return definition

    def define_topic(self, path: str, topic_id: int, payload: Codec) -> TopicDefinition:
        return self.topic(path, topic_id, payload)

    def service(
        self,
        path: str,
        service_id: int,
        *,
        request: Codec | None = None,
        response: Codec | None = None,
    ) -> ServiceDefinition:
        _validate_path(path)
        _validate_id(service_id, "service_id")
        request_codec = Empty if request is None else request
        response_codec = Empty if response is None else response
        _validate_codec(request_codec)
        _validate_codec(response_codec)
        if path in self._services_by_path:
            raise DefinitionError(f"service path {path!r} is already defined")
        if service_id in self._services_by_id:
            existing = self._services_by_id[service_id]
            raise DefinitionError(
                f"service id {service_id} is already used by {existing.path!r}"
            )

        definition = ServiceDefinition(
            path=path,
            service_id=service_id,
            request=request_codec,
            response=response_codec,
        )
        self._services_by_path[path] = definition
        self._services_by_id[service_id] = definition
        return definition

    def define_service(
        self,
        path: str,
        service_id: int,
        *,
        request: Codec | None = None,
        response: Codec | None = None,
    ) -> ServiceDefinition:
        return self.service(
            path,
            service_id,
            request=request,
            response=response,
        )

    def on_topic(
        self, path: str, callback: Callable[[Any], None] | None = None
    ) -> Callable[[Callable[[Any], None]], Callable[[Any], None]] | Callable[[Any], None]:
        def register(func: Callable[[Any], None]) -> Callable[[Any], None]:
            definition = self._require_topic(path)
            self._topic_subscribers.setdefault(definition.topic_id, []).append(func)
            return func

        if callback is not None:
            return register(callback)
        return register

    def on_service(
        self, path: str, callback: Callable[[Any], Any] | None = None
    ) -> Callable[[Callable[[Any], Any]], Callable[[Any], Any]] | Callable[[Any], Any]:
        def register(func: Callable[[Any], Any]) -> Callable[[Any], Any]:
            definition = self._require_service(path)
            self._service_handlers[definition.service_id] = func
            return func

        if callback is not None:
            return register(callback)
        return register

    def publish(self, path: str, value: Any) -> None:
        definition = self._require_topic(path)
        payload = definition.payload.encode(value)
        self._send(PacketType.TOPIC, 0, definition.topic_id, payload)

    def call(self, path: str, request: Any = None, *, timeout: float | None = None) -> Any:
        definition = self._require_service(path)
        pending = _PendingCall(definition=definition, event=threading.Event())
        payload = definition.request.encode(request)

        with self._pending_lock:
            transaction_id = self._next_transaction_id()
            pending_key = (transaction_id, definition.service_id)
            self._pending[pending_key] = pending

        try:
            self._send(
                PacketType.SERVICE_REQUEST,
                transaction_id,
                definition.service_id,
                payload,
            )
            response_timeout = self.response_timeout if timeout is None else timeout
            self._wait_for_response(pending, response_timeout)
            if pending.error is not None:
                raise pending.error
            return pending.response
        finally:
            with self._pending_lock:
                self._pending.pop(pending_key, None)

    def advertise_services(
        self,
        *,
        source_robot_id: int = 0,
        service_ids: Iterable[int] | None = None,
    ) -> None:
        _validate_id(source_robot_id, "source_robot_id")
        advertised_ids = sorted(
            self._services_by_id if service_ids is None else service_ids
        )
        for service_id in advertised_ids:
            if service_id not in self._services_by_id:
                raise DefinitionError(f"unknown service id {service_id}")
        if len(advertised_ids) + 2 > 240:
            raise DefinitionError("too many services to advertise in one SIBCP frame")
        payload = bytes([source_robot_id, len(advertised_ids), *advertised_ids])
        self._send(PacketType.SERVICE_DISCOVERY, 0, 0, payload)

    def poll(self, *, timeout: float = 0.0, max_frames: int | None = 1) -> int:
        handled = 0
        deadline = None if timeout is None else time.monotonic() + timeout

        while max_frames is None or handled < max_frames:
            remaining: float | None
            if deadline is None:
                remaining = None
            else:
                remaining = max(0.0, deadline - time.monotonic())
                if handled > 0 and remaining <= 0.0:
                    break

            frame = self._read_frame(timeout=remaining)
            if frame is None:
                break
            self._dispatch_frame(frame)
            handled += 1

        return handled

    def start_background_reader(self, *, poll_timeout: float = 0.05) -> None:
        if self._reader_thread is not None and self._reader_thread.is_alive():
            return

        self._stop_reader.clear()

        def reader() -> None:
            while not self._stop_reader.is_set():
                try:
                    self.poll(timeout=poll_timeout, max_frames=1)
                except Exception as exc:  # pragma: no cover - defensive logging path
                    self._log(f"SIBCP reader error: {exc}")

        self._reader_thread = threading.Thread(
            target=reader,
            name="sibcp-reader",
            daemon=True,
        )
        self._reader_thread.start()

    def stop_background_reader(self, *, timeout: float = 1.0) -> None:
        self._stop_reader.set()
        if self._reader_thread is not None:
            self._reader_thread.join(timeout=timeout)

    def close(self) -> None:
        self.stop_background_reader()
        close = getattr(self.transport, "close", None)
        if close is not None:
            close()

    def _wait_for_response(self, pending: _PendingCall, timeout: float) -> None:
        if self._reader_is_running():
            if not pending.event.wait(timeout=timeout):
                raise ServiceTimeoutError("SIBCP service call timed out")
            return

        deadline = time.monotonic() + timeout
        while not pending.event.is_set():
            remaining = deadline - time.monotonic()
            if remaining <= 0.0:
                raise ServiceTimeoutError("SIBCP service call timed out")
            self.poll(timeout=remaining, max_frames=1)

    def _read_frame(self, *, timeout: float | None) -> Frame | None:
        deadline = None if timeout is None else time.monotonic() + timeout

        while True:
            chunk = self.transport.read(1)
            if chunk:
                for value in chunk:
                    frame = self._parser.push_byte(value)
                    if frame is not None:
                        return frame
                continue

            if deadline is not None and time.monotonic() >= deadline:
                return None
            if timeout == 0.0:
                return None
            time.sleep(0.001)

    def _dispatch_frame(self, frame: Frame) -> None:
        if frame.packet_type == PacketType.TOPIC:
            self._handle_topic(frame)
        elif frame.packet_type == PacketType.SERVICE_REQUEST:
            self._handle_service_request(frame)
        elif frame.packet_type == PacketType.SERVICE_RESPONSE:
            self._handle_service_response(frame)
        elif frame.packet_type == PacketType.SERVICE_DISCOVERY:
            self._handle_discovery(frame)
        else:
            self._log(f"ignored SIBCP packet type {packet_type_name(frame.packet_type)}")

    def _handle_topic(self, frame: Frame) -> None:
        definition = self._topics_by_id.get(frame.identifier_id)
        if definition is None:
            self._log(f"ignored unknown topic id {frame.identifier_id}")
            return

        try:
            value = definition.payload.decode(frame.payload)
        except CodecError as exc:
            self._log(f"ignored malformed topic {definition.path}: {exc}")
            return

        for callback in list(self._topic_subscribers.get(frame.identifier_id, [])):
            try:
                callback(value)
            except Exception as exc:  # pragma: no cover - application callback path
                self._log(f"topic callback failed for {definition.path}: {exc}")

    def _handle_service_request(self, frame: Frame) -> None:
        definition = self._services_by_id.get(frame.identifier_id)
        handler = self._service_handlers.get(frame.identifier_id)
        if definition is None or handler is None:
            self._send_service_response(
                frame.transaction_id,
                frame.identifier_id,
                SERVICE_STATUS_UNSUPPORTED,
                b"",
            )
            return

        try:
            request = definition.request.decode(frame.payload)
            result = handler(request)
            status_code = SERVICE_STATUS_OK
            value = result
            if isinstance(result, ServiceResult):
                status_code = result.status_code
                value = result.value

            payload = b""
            if status_code == SERVICE_STATUS_OK:
                payload = definition.response.encode(value)
            self._send_service_response(
                frame.transaction_id,
                frame.identifier_id,
                status_code,
                payload,
            )
        except Exception as exc:
            self._log(f"service handler failed for {definition.path}: {exc}")
            self._send_service_response(
                frame.transaction_id,
                frame.identifier_id,
                SERVICE_STATUS_ERROR,
                b"",
            )

    def _handle_service_response(self, frame: Frame) -> None:
        with self._pending_lock:
            pending = self._pending.get((frame.transaction_id, frame.identifier_id))

        if pending is None:
            self._log(
                "ignored unexpected service response "
                f"tx={frame.transaction_id} id={frame.identifier_id}"
            )
            return

        if len(frame.payload) < 1:
            pending.error = CodecError("service response is missing status byte")
            pending.event.set()
            return

        status_code = frame.payload[0]
        if status_code != SERVICE_STATUS_OK:
            pending.error = ServiceCallError(status_code)
            pending.event.set()
            return

        try:
            pending.response = pending.definition.response.decode(frame.payload[1:])
        except CodecError as exc:
            pending.error = exc
        finally:
            pending.event.set()

    def _handle_discovery(self, frame: Frame) -> None:
        if len(frame.payload) < 2:
            self._log("ignored malformed service discovery frame")
            return

        source_robot_id = frame.payload[0]
        total_services = frame.payload[1]
        service_ids = frame.payload[2 : 2 + total_services]
        if len(service_ids) != total_services:
            self._log("ignored truncated service discovery frame")
            return

        self.discovered_services[source_robot_id] = set(service_ids)

    def _send_service_response(
        self,
        transaction_id: int,
        service_id: int,
        status_code: int,
        payload: bytes,
    ) -> None:
        _validate_id(status_code, "status_code")
        self._send(
            PacketType.SERVICE_RESPONSE,
            transaction_id,
            service_id,
            bytes([status_code]) + payload,
        )

    def _send(
        self,
        packet_type: PacketType,
        transaction_id: int,
        identifier_id: int,
        payload: bytes,
    ) -> None:
        frame = encode_frame(packet_type, transaction_id, identifier_id, payload)
        with self._send_lock:
            self.transport.write(frame)

    def _next_transaction_id(self) -> int:
        self._transaction_id = (self._transaction_id + 1) & 0xFFFF
        if self._transaction_id == 0:
            self._transaction_id = 1
        return self._transaction_id

    def _reader_is_running(self) -> bool:
        return self._reader_thread is not None and self._reader_thread.is_alive()

    def _require_topic(self, path: str) -> TopicDefinition:
        try:
            return self._topics_by_path[path]
        except KeyError as exc:
            raise DefinitionError(f"unknown topic {path!r}") from exc

    def _require_service(self, path: str) -> ServiceDefinition:
        try:
            return self._services_by_path[path]
        except KeyError as exc:
            raise DefinitionError(f"unknown service {path!r}") from exc

    def _log(self, message: str) -> None:
        if self.logger is not None:
            self.logger(message)


def _validate_path(path: str) -> None:
    if not isinstance(path, str) or not path.startswith("/") or len(path) < 2:
        raise DefinitionError("paths must be strings like '/ball_in_your_vision'")


def _validate_id(value: int, name: str) -> None:
    if not isinstance(value, int) or not 0 <= value <= 0xFF:
        raise DefinitionError(f"{name} must fit in uint8")


def _validate_codec(codec: Codec) -> None:
    if not isinstance(codec, Codec):
        raise DefinitionError("payload definitions must use sibcp Codec instances")
