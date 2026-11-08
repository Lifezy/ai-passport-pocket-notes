#!/usr/bin/env python3
"""Apply private Pocket Notes settings over USB; this does not flash firmware."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import time
from urllib.parse import urlsplit

ROOT = Path(__file__).resolve().parents[1]
LIMITS = {
    "wifi_ssid": 32, "wifi_password": 64, "companion_url": 192,
    "companion_token": 128, "nas_host": 63, "nas_community": 64,
    "genshin_uid": 15, "genshin_region": 31, "genshin_cookie": 511,
    "genshin_widget_cookie": 511, "genshin_device_id": 64, "genshin_device_fp": 64,
    "home_url": 192, "home_token": 512,
}


def load_settings(path: Path, token_path: Path) -> bytes:
    values = json.loads(path.read_text(encoding="utf-8"))
    return validate_settings(values, token_path)


def validate_settings(values: dict, token_path: Path | None = None) -> bytes:
    """Validate a partial update without writing private values to a file."""
    if not isinstance(values, dict) or not values:
        raise ValueError("The configuration must be a non-empty JSON object")
    values = dict(values)
    if values.get("companion_url") and not values.get("companion_token"):
        if token_path is None:
            raise ValueError("A companion pairing token is required")
        values["companion_token"] = token_path.read_text(encoding="ascii").strip()
    for key, value in values.items():
        if key not in LIMITS or not isinstance(value, str):
            raise ValueError("Unknown configuration key or non-text value")
        if len(value.encode("utf-8")) > LIMITS[key] or any(ord(char) < 32 for char in value):
            raise ValueError(f"Invalid length or control character in {key}")
        if "YOUR_" in value or "COMPUTER_LAN_IP" in value:
            raise ValueError(f"Replace the example placeholder in {key}")
    if "wifi_ssid" in values and not values["wifi_ssid"]:
        raise ValueError("wifi_ssid must be filled in")
    password = values.get("wifi_password", "")
    if password and (len(password.encode("utf-8")) < 8 or
                     (len(password.encode("utf-8")) == 64 and any(char not in "0123456789abcdefABCDEF" for char in password))):
        raise ValueError("Invalid Wi-Fi password length or raw PSK")
    for key in ("companion_url", "home_url"):
        if values.get(key):
            url = urlsplit(values[key])
            if url.scheme not in ("http", "https") or not url.hostname or url.username or url.password or url.query or url.fragment:
                raise ValueError(f"Invalid base URL in {key}")
    payload = json.dumps(values, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
    if len(payload) > 4096:
        raise ValueError("Configuration exceeds the device's 4096-byte limit")
    return payload


def apply_settings(connection, payload: bytes, timeout: float = 12):
    # Short USB chunks avoid overrunning a polling native-USB receive buffer.
    for start in range(0, len(payload), 64):
        connection.write(payload[start:start + 64])
        time.sleep(0.015)
    connection.write(b"\n")
    connection.flush()
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        line = connection.readline(2048).strip()
        if line == b"POCKET_CONFIG_OK":
            return
        if line == b"POCKET_CONFIG_REJECTED":
            raise ValueError("Device rejected settings; existing settings were retained")
        # Never echo arbitrary serial lines: console echo may contain credentials.
    raise TimeoutError("No settings acknowledgement; verify port and Pocket Notes firmware")


def send_settings(port: str, payload: bytes):
    """Open only the explicitly selected port; avoid asserting reset lines."""
    import serial
    connection = serial.Serial(port=None, baudrate=115200, timeout=0.25, write_timeout=3)
    connection.dtr = False
    connection.rts = False
    connection.port = port
    try:
        connection.open()
        apply_settings(connection, payload)
    finally:
        connection.close()


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port")
    parser.add_argument("--config", type=Path, default=ROOT / "build/private/device.json")
    parser.add_argument("--token-file", type=Path, default=ROOT / "build/private/companion.token")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--create-template", action="store_true")
    args = parser.parse_args(argv)
    if args.create_template:
        args.config.parent.mkdir(parents=True, exist_ok=True)
        example = {"wifi_ssid": "YOUR_WIFI_SSID", "wifi_password": "YOUR_WIFI_PASSWORD",
                   "companion_url": "http://COMPUTER_LAN_IP:8765", "nas_host": "192.168.1.250",
                   "nas_community": "", "home_url": "", "home_token": ""}
        with args.config.open("x", encoding="utf-8") as target:
            json.dump(example, target, ensure_ascii=False, indent=2)
        print("Private configuration template created; fill it in locally.")
        return 0
    payload = load_settings(args.config, args.token_file)
    if args.dry_run:
        print(f"Configuration: PASS ({len(payload)} bytes; values are not printed)")
        return 0
    if not args.port:
        parser.error("--port is required when applying settings")
    try:
        import serial
    except ImportError:
        parser.error("pyserial is required; use the activated ESP-IDF Python environment")
    send_settings(args.port, payload)
    print("Settings saved. The application is restarting; no firmware was flashed.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, TimeoutError):
        # Do not include exception text from JSON or serial libraries: it may
        # quote a private configuration value. A non-secret reason is in --help.
        raise SystemExit("Settings were not applied; check the private file, device and port.")
