"""
RoboClawTCP — Basicmicro subclass that replaces pyserial with a direct TCP
socket connection to a USR-K6 (or similar) Ethernet-to-Serial converter.

This is the only class that touches the transport layer.  All Packet Serial
commands, CRC, retry logic and the full controller API are inherited from
the upstream Basicmicro class without modification.

IMPORTANT — Constructor compatibility:
  The upstream driver calls:  Basicmicro(self.port, self.baud)
  where self.port is a string like "tcp://192.168.68.60:8234".

  RoboClawTCP accepts the SAME (comport, rate, ...) signature.
  If comport starts with "tcp://", it parses host:port from the URL.
  Otherwise it tries to interpret comport as "host:port".

Usage:
    # Via monkey-patch (the driver passes the port parameter string):
    dev = RoboClawTCP("tcp://192.168.68.60:8234", 115200)
    dev.Open()

    # Direct (legacy API):
    dev = RoboClawTCP("192.168.68.60:8234", 0)
    dev.Open()
"""

import logging
from urllib.parse import urlparse
from basicmicro.controller import Basicmicro
from roboclaw_tcp_adapter.tcp_port import RoboClawTCPPort

logger = logging.getLogger(__name__)


def _parse_tcp_address(comport: str) -> tuple:
    """Extract (host, port) from a comport string.

    Accepted formats:
        "tcp://192.168.68.60:8234"
        "192.168.68.60:8234"
    """
    s = comport.strip()

    if s.startswith("tcp://"):
        parsed = urlparse(s)
        host = parsed.hostname
        port = parsed.port
        if host and port:
            return host, int(port)
        raise ValueError(f"Invalid tcp:// URL: {comport}")

    if ":" in s:
        parts = s.rsplit(":", 1)
        return parts[0], int(parts[1])

    raise ValueError(
        f"Cannot parse TCP address from '{comport}'. "
        "Expected 'tcp://host:port' or 'host:port'."
    )


class RoboClawTCP(Basicmicro):
    """Basicmicro controller accessed over TCP instead of a local serial port.

    Constructor signature matches Basicmicro(comport, rate, timeout, retries,
    verbose) exactly, so the monkey-patched driver passes its ROS parameters
    without any awareness of the TCP transport underneath.

    Only Open() is overridden — the rest of the 150+ command methods, CRC
    calculation, retry logic and reconnect() are inherited unchanged.
    """

    def __init__(
        self,
        comport: str,
        rate: int = 0,
        timeout: float = 0.5,
        retries: int = 2,
        verbose: bool = False,
    ):
        """
        Args:
            comport: TCP address as "tcp://host:port" or "host:port"
            rate:    Ignored (baud rate is set on the USR-K6 hardware side)
            timeout: Inter-byte timeout for socket recv (seconds)
            retries: Number of command retries on CRC failure
            verbose: Enable DEBUG-level logging
        """
        self._tcp_host, self._tcp_port = _parse_tcp_address(comport)

        super().__init__(comport, rate, timeout, retries, verbose)

        logger.info(
            "RoboClawTCP initialized: %s:%d (baud %d ignored, timeout %.3fs)",
            self._tcp_host,
            self._tcp_port,
            rate,
            timeout,
        )

    def Open(self) -> bool:
        """Open a TCP connection to the USR-K6 converter instead of a serial
        port.  On failure returns False (no exception), matching the upstream
        Basicmicro.Open() contract."""
        self.close()
        try:
            self._port = RoboClawTCPPort(
                self._tcp_host,
                self._tcp_port,
                inter_byte_timeout=self.timeout,
            )
            self._connected = True
            logger.info(
                "RoboClawTCP connected to %s:%d", self._tcp_host, self._tcp_port
            )
            return True
        except Exception as exc:
            logger.error("RoboClawTCP connection failed: %s", exc)
            self._port = None
            self._connected = False
            return False
