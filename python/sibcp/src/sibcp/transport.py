"""Transport adapters for SIBCP nodes."""

from __future__ import annotations

import queue
import time
from typing import Any


class SerialTransport:
    """pyserial-backed byte transport.

    Install with:

        python -m pip install -e python/sibcp[serial]
    """

    def __init__(
        self,
        port: str,
        baudrate: int = 460800,
        *,
        timeout: float = 0.02,
        **serial_kwargs: Any,
    ) -> None:
        try:
            import serial
        except ImportError as exc:
            raise RuntimeError(
                "SerialTransport requires pyserial. Install with "
                "`python -m pip install -e python/sibcp[serial]`."
            ) from exc

        self.serial = serial.Serial(
            port=port,
            baudrate=baudrate,
            timeout=timeout,
            **serial_kwargs,
        )

    def read(self, size: int = 1) -> bytes:
        return self.serial.read(size)

    def write(self, data: bytes | bytearray | memoryview) -> int:
        return int(self.serial.write(bytes(data)))

    def close(self) -> None:
        self.serial.close()


class MemoryTransport:
    """In-memory transport pair for tests and examples."""

    def __init__(self, *, read_timeout: float = 0.01) -> None:
        self.read_timeout = read_timeout
        self._rx: queue.Queue[int] = queue.Queue()
        self._peer: MemoryTransport | None = None
        self._closed = False

    @classmethod
    def pair(
        cls, *, read_timeout: float = 0.01
    ) -> tuple[MemoryTransport, MemoryTransport]:
        left = cls(read_timeout=read_timeout)
        right = cls(read_timeout=read_timeout)
        left._peer = right
        right._peer = left
        return left, right

    def read(self, size: int = 1) -> bytes:
        if self._closed or size <= 0:
            return b""

        result = bytearray()
        deadline = time.monotonic() + self.read_timeout
        while len(result) < size:
            timeout = max(0.0, deadline - time.monotonic()) if not result else 0.0
            try:
                result.append(self._rx.get(timeout=timeout))
            except queue.Empty:
                break
        return bytes(result)

    def write(self, data: bytes | bytearray | memoryview) -> int:
        if self._closed:
            return 0
        if self._peer is None:
            raise RuntimeError("MemoryTransport is not connected to a peer")
        raw = bytes(data)
        for value in raw:
            self._peer._rx.put(value)
        return len(raw)

    def close(self) -> None:
        self._closed = True
