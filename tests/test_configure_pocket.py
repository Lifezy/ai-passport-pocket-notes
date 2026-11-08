import importlib.util
import json
from pathlib import Path
import tempfile
import types
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location("configure", Path(__file__).resolve().parents[1] / "tools/configure_pocket.py")
SETUP = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SETUP)


class SetupTests(unittest.TestCase):
    def test_utf8_byte_limit_and_local_token(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            config, token = root / "settings.json", root / "pairing.token"
            token.write_text("test-pairing-token-with-sufficient-length", encoding="ascii")
            config.write_text(json.dumps({"wifi_ssid": "test", "companion_url": "http://127.0.0.1:8765"}), encoding="utf-8")
            values = json.loads(SETUP.load_settings(config, token))
            self.assertEqual(values["companion_token"], token.read_text(encoding="ascii"))
            config.write_text(json.dumps({"wifi_ssid": "中" * 11}), encoding="utf-8")
            with self.assertRaises(ValueError):
                SETUP.load_settings(config, token)

    def test_rejects_bad_keys_header_injection_and_placeholders(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "settings.json"
            for values in [{"wrong": "value"}, {"home_token": "bad\r\nheader"},
                           {"wifi_ssid": "YOUR_WIFI_SSID"}, {"home_url": "http://user:password@localhost"}]:
                path.write_text(json.dumps(values), encoding="utf-8")
                with self.assertRaises(ValueError):
                    SETUP.load_settings(path, path)

    def test_serial_acknowledgement_is_required(self):
        class Serial:
            def __init__(self, lines): self.lines = iter(lines); self.sent = bytearray()
            def write(self, data): self.sent.extend(data)
            def flush(self): pass
            def readline(self, _): return next(self.lines, b"")
        with patch.object(SETUP.time, "sleep"):
            connection = Serial([b'{"echo":"private"}\n', b"POCKET_CONFIG_OK\n"])
            SETUP.apply_settings(connection, b'{"wifi_ssid":"test"}')
            self.assertEqual(connection.sent, b'{"wifi_ssid":"test"}\n')
            with self.assertRaises(ValueError):
                SETUP.apply_settings(Serial([b"POCKET_CONFIG_REJECTED"]), b"{}")

    def test_wifi_only_update_needs_no_other_configuration_or_file(self):
        values = {"wifi_ssid": "家庭网络", "wifi_password": "test-password"}
        self.assertEqual(json.loads(SETUP.validate_settings(values)), values)
        self.assertEqual(SETUP.validate_settings({"wifi_ssid": "open", "wifi_password": ""}),
                         b'{"wifi_ssid":"open","wifi_password":""}')
        with self.assertRaises(ValueError):
            SETUP.validate_settings({"wifi_ssid": "test", "wifi_password": "中" * 21 + "x"})
        SETUP.validate_settings({"wifi_ssid": "test", "wifi_password": "a" * 64})

    def test_selected_port_reset_lines_and_failure_cleanup(self):
        class Serial:
            def __init__(self, **_): self.closed = False
            def open(self):
                self.assertions = (self.port, self.dtr, self.rts)
            def close(self): self.closed = True
        connection = Serial()
        module = types.SimpleNamespace(Serial=lambda **_: connection)
        with patch.dict("sys.modules", {"serial": module}), patch.object(SETUP, "apply_settings", side_effect=TimeoutError):
            with self.assertRaises(TimeoutError):
                SETUP.send_settings("COM_TEST", b"{}")
        self.assertEqual(connection.assertions, ("COM_TEST", False, False))
        self.assertTrue(connection.closed)


if __name__ == "__main__":
    unittest.main()
