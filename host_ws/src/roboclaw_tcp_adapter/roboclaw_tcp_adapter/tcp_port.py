"""
RoboClawTCPPort — Thin socket adapter implementing the serial.Serial API
surface used by basicmicro_python.

The basicmicro_python library calls the following methods on its internal
_port object: write(), read(), flushInput(), reset_input_buffer(),
reset_output_buffer(), close(), and checks is_open.  This class provides
those exact methods backed by a raw TCP socket, allowing transparent
communication through an Ethernet-to-Serial converter (USR-K6).

TCP_NODELAY is enabled so that each write() call produces an immediate TCP
segment.  The USR-K6 collects arriving segments in its internal UART buffer
and streams them out at 115200 baud (~86 us/byte), well within the
RoboClaw's 10 ms inter-byte timeout on a local network.
"""

import socket
import logging

logger = logging.getLogger(__name__)


class RoboClawTCPPort:
    """Drop-in TCP replacement for serial.Serial, limited to the API surface
    actually used by basicmicro_python's Basicmicro class."""

    def __init__(self, host: str, port: int, inter_byte_timeout: float = 0.05):
        """
        Args:
            host: USR-K6 IP address (e.g. "192.168.68.60")
            port: USR-K6 TCP port  (e.g. 8234)
            inter_byte_timeout: Socket recv timeout in seconds.  Must exceed
                the worst-case network jitter on the LAN.  50 ms is safe for
                a local 100 Mbit Ethernet segment.
        """
        self._host = host
        self._port_num = port
        self._timeout = inter_byte_timeout

        self._sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self._sock.settimeout(inter_byte_timeout)

        logger.info("Connecting to %s:%d (timeout=%.3fs)", host, port, inter_byte_timeout)
        self._sock.connect((host, port))
        self.is_open = True
        logger.info("TCP connection established to %s:%d", host, port)

    def write(self, data: bytes) -> int:
        """Send data to the USR-K6.  Uses sendall() for atomic delivery."""
        self._sock.sendall(data)
        return len(data)

    def read(self, n: int = 1) -> bytes:
        """Read up to n bytes.  Returns fewer bytes on timeout (matching
        pyserial behaviour where short reads signal timeout)."""
        result = b""
        while len(result) < n:
            try:
                chunk = self._sock.recv(n - len(result))
                if not chunk:
                    break
                result += chunk
            except socket.timeout:
                break
        return result

    def flushInput(self) -> None:
        """Non-blocking drain of the receive buffer.  Unlike pyserial's
        flushInput() which discards pending data instantly, this reads and
        discards without blocking — preventing loss of valid response bytes
        that arrive with slight network jitter."""
        self._sock.setblocking(False)
        try:
            while True:
                chunk = self._sock.recv(4096)
                if not chunk:
                    break
        except BlockingIOError:
            pass
        finally:
            self._sock.setblocking(True)
            self._sock.settimeout(self._timeout)

    def reset_input_buffer(self) -> None:
        """Alias for flushInput() — pyserial compatibility."""
        self.flushInput()

    def reset_output_buffer(self) -> None:
        """No-op.  TCP send buffer is managed by the OS kernel."""
        pass

    def close(self) -> None:
        """Close the TCP connection."""
        if self.is_open:
            try:
                self._sock.close()
                logger.info("TCP connection closed to %s:%d", self._host, self._port_num)
            except Exception as exc:
                logger.warning("Error closing TCP socket: %s", exc)
            finally:
                self.is_open = False
