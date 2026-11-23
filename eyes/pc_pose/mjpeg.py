"""Low-latency MJPEG reader for the ESP32 camera stream."""

import threading
import time
import http.client
import os
import socket
from urllib.parse import urlsplit


class _DirectHttpConnection(http.client.HTTPConnection):
    """HTTP connection pinned to the physical WLAN interface on Windows."""

    def connect(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.settimeout(self.timeout)
        if os.name == "nt" and hasattr(socket, "IP_UNICAST_IF"):
            # Windows may otherwise select the Mihomo/TUN default route for
            # 192.168.4.1. The ESP32 AP is on the WLAN adapter (ifIndex 12).
            raw_index = os.environ.get("ROBOT_WIFI_IFINDEX", "12")
            try:
                interface_index = int(raw_index)
            except ValueError:
                interface_index = 12
            self.sock.setsockopt(socket.IPPROTO_IP, socket.IP_UNICAST_IF, interface_index)
        self.sock.connect((self.host, self.port))


class LatestMjpegReader:
    """Read an HTTP MJPEG stream in a background thread and keep one frame."""

    def __init__(self, url, reconnect_delay=1.0):
        self.url = str(url)
        self.reconnect_delay = float(reconnect_delay)
        self._condition = threading.Condition()
        self._latest_frame = None
        self._sequence = 0
        self._error = None
        self._stopped = threading.Event()
        self._thread = threading.Thread(target=self._run, name="mjpeg-reader", daemon=True)

    def start(self):
        self._thread.start()
        return self

    @property
    def error(self):
        with self._condition:
            return self._error

    def read(self, last_sequence=0, timeout=10.0):
        with self._condition:
            deadline = time.monotonic() + float(timeout)
            while not self._stopped.is_set() and self._sequence <= last_sequence:
                remaining = deadline - time.monotonic()
                if remaining <= 0.0:
                    return False, None, last_sequence
                self._condition.wait(remaining)
            if self._sequence <= last_sequence or self._latest_frame is None:
                return False, None, last_sequence
            return True, self._latest_frame, self._sequence

    def close(self):
        self._stopped.set()
        with self._condition:
            self._condition.notify_all()
        self._thread.join(timeout=2.0)

    def _run(self):
        import cv2
        import numpy as np

        while not self._stopped.is_set():
            try:
                parsed = urlsplit(self.url)
                if parsed.scheme.lower() != "http" or not parsed.hostname:
                    raise ValueError("MJPEG source must be an HTTP URL")
                path = parsed.path or "/"
                if parsed.query:
                    path += "?" + parsed.query
                connection = _DirectHttpConnection(
                    parsed.hostname,
                    parsed.port or 80,
                    timeout=10.0,
                )
                connection.request(
                    "GET",
                    path,
                    headers={
                        "Cache-Control": "no-cache",
                        "Connection": "keep-alive",
                        "User-Agent": "robot-pose",
                    },
                )
                response = connection.getresponse()
                if response.status != 200:
                    raise ConnectionError(f"MJPEG HTTP status {response.status}")
                try:
                    buffer = bytearray()
                    while not self._stopped.is_set():
                        chunk = response.read(8192)
                        if not chunk:
                            raise ConnectionError("MJPEG stream closed")
                        buffer.extend(chunk)

                        while True:
                            start = buffer.find(b"\xff\xd8")
                            if start < 0:
                                if len(buffer) > 8192:
                                    del buffer[:-2]
                                break
                            end = buffer.find(b"\xff\xd9", start + 2)
                            if end < 0:
                                if start > 0:
                                    del buffer[:start]
                                break

                            jpeg = bytes(buffer[start:end + 2])
                            del buffer[:end + 2]
                            frame = cv2.imdecode(
                                np.frombuffer(jpeg, dtype=np.uint8), cv2.IMREAD_COLOR
                            )
                            if frame is None:
                                continue
                            with self._condition:
                                self._latest_frame = frame
                                self._sequence += 1
                                self._error = None
                                self._condition.notify_all()
                finally:
                    response.close()
                    connection.close()
            except Exception as exc:
                with self._condition:
                    self._error = str(exc)
                    self._condition.notify_all()
                if not self._stopped.wait(self.reconnect_delay):
                    continue
