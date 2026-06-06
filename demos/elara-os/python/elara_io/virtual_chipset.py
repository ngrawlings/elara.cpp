from __future__ import annotations

from dataclasses import dataclass, field
from enum import IntEnum
from queue import Empty, Queue
from time import monotonic_ns
from typing import Dict, Iterable, Optional


HOST_KERNEL_ID = -1
HOST_WORKER_ID = -1


class IoOp(IntEnum):
    NOP = 0
    READ = 1
    WRITE = 2
    SUBSCRIBE = 3
    UNSUBSCRIBE = 4
    EVENT = 5
    ACK = 6
    ERROR = 7


@dataclass(frozen=True)
class IoRoute:
    kernel_id: int
    worker_id: int

    @classmethod
    def host(cls) -> "IoRoute":
        return cls(HOST_KERNEL_ID, HOST_WORKER_ID)


@dataclass
class IoFrame:
    source: IoRoute
    target: IoRoute
    device: str
    op: IoOp
    sequence: int
    payload: bytes = b""
    timestamp_ns: int = field(default_factory=monotonic_ns)

    def ack(self, payload: bytes = b"") -> "IoFrame":
        return IoFrame(
            source=self.target,
            target=self.source,
            device=self.device,
            op=IoOp.ACK,
            sequence=self.sequence,
            payload=payload,
        )

    def error(self, message: str) -> "IoFrame":
        return IoFrame(
            source=self.target,
            target=self.source,
            device=self.device,
            op=IoOp.ERROR,
            sequence=self.sequence,
            payload=message.encode("utf-8"),
        )


class IoDevice:
    def __init__(self, name: str) -> None:
        self.name = name

    def handle(self, frame: IoFrame) -> IoFrame:
        return frame.error(f"{self.name} does not implement {frame.op.name}")


class StorageController(IoDevice):
    def __init__(self) -> None:
        super().__init__("storage")
        self.blocks: Dict[bytes, bytes] = {}

    def handle(self, frame: IoFrame) -> IoFrame:
        if frame.op == IoOp.WRITE:
            key, _, body = frame.payload.partition(b"\0")
            if not key:
                return frame.error("storage write requires a key")
            self.blocks[key] = body
            return frame.ack(b"stored")
        if frame.op == IoOp.READ:
            return frame.ack(self.blocks.get(frame.payload, b""))
        return super().handle(frame)


class DisplayController(IoDevice):
    def __init__(self) -> None:
        super().__init__("display")
        self.last_surface_frame: bytes = b""

    def handle(self, frame: IoFrame) -> IoFrame:
        if frame.op == IoOp.WRITE:
            self.last_surface_frame = frame.payload
            return frame.ack(b"display frame accepted")
        if frame.op == IoOp.READ:
            return frame.ack(self.last_surface_frame)
        return super().handle(frame)


class InputController(IoDevice):
    def __init__(self) -> None:
        super().__init__("input")
        self.events: "Queue[bytes]" = Queue()

    def push_event(self, payload: bytes) -> None:
        self.events.put(payload)

    def handle(self, frame: IoFrame) -> IoFrame:
        if frame.op == IoOp.READ:
            try:
                return frame.ack(self.events.get_nowait())
            except Empty:
                return frame.ack(b"")
        return super().handle(frame)


class NetworkController(IoDevice):
    def __init__(self) -> None:
        super().__init__("network")
        self.egress: "Queue[bytes]" = Queue()

    def handle(self, frame: IoFrame) -> IoFrame:
        if frame.op == IoOp.WRITE:
            self.egress.put(frame.payload)
            return frame.ack(b"network packet queued")
        return super().handle(frame)


class ClockController(IoDevice):
    def __init__(self) -> None:
        super().__init__("clock")

    def handle(self, frame: IoFrame) -> IoFrame:
        if frame.op == IoOp.READ:
            return frame.ack(str(monotonic_ns()).encode("ascii"))
        return super().handle(frame)


class VirtualIoChipset:
    def __init__(self) -> None:
        self.devices: Dict[str, IoDevice] = {}
        self.ingress: "Queue[IoFrame]" = Queue()
        self.egress: "Queue[IoFrame]" = Queue()
        self._next_sequence = 1

    def register(self, device: IoDevice) -> None:
        if device.name in self.devices:
            raise ValueError(f"duplicate IO device: {device.name}")
        self.devices[device.name] = device

    def device_names(self) -> Iterable[str]:
        return tuple(sorted(self.devices.keys()))

    def next_sequence(self) -> int:
        value = self._next_sequence
        self._next_sequence += 1
        return value

    def submit(self, frame: IoFrame) -> None:
        self.ingress.put(frame)

    def request_from_host(self, device: str, op: IoOp, payload: bytes = b"") -> IoFrame:
        return IoFrame(
            source=IoRoute.host(),
            target=IoRoute(kernel_id=0, worker_id=0),
            device=device,
            op=op,
            sequence=self.next_sequence(),
            payload=payload,
        )

    def pump_once(self) -> Optional[IoFrame]:
        try:
            frame = self.ingress.get_nowait()
        except Empty:
            return None

        device = self.devices.get(frame.device)
        if device is None:
            response = frame.error(f"unknown IO device: {frame.device}")
        else:
            response = device.handle(frame)
        self.egress.put(response)
        return response

    def pump_all(self) -> int:
        count = 0
        while self.pump_once() is not None:
            count += 1
        return count


def build_default_chipset() -> VirtualIoChipset:
    chipset = VirtualIoChipset()
    chipset.register(StorageController())
    chipset.register(DisplayController())
    chipset.register(InputController())
    chipset.register(NetworkController())
    chipset.register(ClockController())
    return chipset
