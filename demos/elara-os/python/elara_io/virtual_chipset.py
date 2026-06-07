from __future__ import annotations

from dataclasses import dataclass, field
from enum import IntEnum
import os
from pathlib import Path
from queue import Empty, Queue
import struct
from time import monotonic_ns, time_ns
from datetime import datetime, timezone
from typing import Dict, Iterable, Optional


HOST_KERNEL_ID = -1
HOST_WORKER_ID = -1
DEFAULT_BLOCK_SIZE = 4096
DEFAULT_DRIVE_SIZE_BYTES = 1024 * 1024 * 1024


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


@dataclass(frozen=True)
class VirtualDrive:
    drive_id: int
    path: Path
    size_bytes: int
    block_size: int = DEFAULT_BLOCK_SIZE

    @property
    def block_count(self) -> int:
        return self.size_bytes // self.block_size


def build_block_request(drive_id: int, block_index: int, block_count: int, body: bytes = b"") -> bytes:
    return struct.pack("<IQI", drive_id, block_index, block_count) + body


class PersistentBlockIoController(IoDevice):
    def __init__(self, drives: Optional[Iterable[VirtualDrive]] = None) -> None:
        super().__init__("block_io")
        if drives is None:
            root_drive_path = Path(
                os.environ.get(
                    "ELARA_OS_ROOT_DRIVE",
                    str(Path.home() / ".elaraos" / "root"),
                )
            )
            drives = (
                VirtualDrive(
                    drive_id=1,
                    path=root_drive_path,
                    size_bytes=DEFAULT_DRIVE_SIZE_BYTES,
                ),
            )
        self.drives: Dict[int, VirtualDrive] = {drive.drive_id: drive for drive in drives}
        for drive in self.drives.values():
            self._ensure_drive_exists(drive)

    def _ensure_drive_exists(self, drive: VirtualDrive) -> None:
        drive.path.parent.mkdir(parents=True, exist_ok=True)
        if drive.path.exists():
            if not drive.path.is_file():
                raise ValueError(f"virtual drive path is not a file: {drive.path}")
            return
        with drive.path.open("wb") as handle:
            handle.truncate(drive.size_bytes)

    def _parse_request(self, frame: IoFrame) -> tuple[VirtualDrive, int, int, bytes]:
        if len(frame.payload) < 16:
            raise ValueError("block request must contain drive_id, block_index, and block_count")
        drive_id, block_index, block_count = struct.unpack("<IQI", frame.payload[:16])
        drive = self.drives.get(drive_id)
        if drive is None:
            raise ValueError(f"unknown block drive id: {drive_id}")
        byte_offset = block_index * drive.block_size
        byte_count = block_count * drive.block_size
        if byte_count < 0 or byte_offset < 0:
            raise ValueError("negative block window is invalid")
        if byte_offset + byte_count > drive.size_bytes:
            raise ValueError("block window exceeds virtual drive bounds")
        return drive, byte_offset, byte_count, frame.payload[16:]

    def handle(self, frame: IoFrame) -> IoFrame:
        try:
            drive, byte_offset, byte_count, body = self._parse_request(frame)
        except ValueError as exc:
            return frame.error(str(exc))

        if frame.op == IoOp.READ:
            with drive.path.open("rb") as handle:
                handle.seek(byte_offset)
                return frame.ack(handle.read(byte_count))
        if frame.op == IoOp.WRITE:
            if len(body) != byte_count:
                return frame.error(
                    f"block write body size mismatch: expected {byte_count} bytes, got {len(body)}"
                )
            with drive.path.open("r+b") as handle:
                handle.seek(byte_offset)
                handle.write(body)
                handle.flush()
            return frame.ack(struct.pack("<III", drive.drive_id, drive.block_size, byte_count))
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
            now = datetime.now(timezone.utc)
            payload = struct.pack(
                "<HBBBBBBQ",
                now.year,
                now.month,
                now.day,
                now.hour,
                now.minute,
                now.second,
                now.weekday(),
                time_ns(),
            )
            return frame.ack(payload)
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
    chipset.register(PersistentBlockIoController())
    chipset.register(DisplayController())
    chipset.register(InputController())
    chipset.register(NetworkController())
    chipset.register(ClockController())
    return chipset
