import importlib.util
import json
from pathlib import Path
import threading
import time
import unittest
import sys
from unittest.mock import patch
from urllib.error import HTTPError
from urllib.request import ProxyHandler, Request, build_opener

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
SPEC = importlib.util.spec_from_file_location("companion", Path(__file__).resolve().parents[1] / "tools/codex_companion.py")
COMPANION = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(COMPANION)


class AllowanceTests(unittest.TestCase):
    def test_pairing_is_explicit_bounded_expiring_and_single_use(self):
        server = COMPANION.CompanionServer(("127.0.0.1", 0), COMPANION.Snapshot(), "test-token", 180)
        try:
            self.assertIsNone(server.redeem_pairing("000000"))
            code = server.begin_pairing()
            self.assertEqual(len(code), 6)
            self.assertEqual(server.redeem_pairing(code), "test-token")
            self.assertIsNone(server.redeem_pairing(code))
            code = server.begin_pairing()
            for _ in range(5): self.assertIsNone(server.redeem_pairing("wrong"))
            self.assertIsNone(server.redeem_pairing(code))
            code = server.begin_pairing()
            with patch.object(COMPANION.time, "monotonic", return_value=server.pairing_deadline):
                self.assertIsNone(server.redeem_pairing(code))
        finally:
            server.server_close()

    def test_pairing_http_hands_out_only_the_display_token_once(self):
        server = COMPANION.CompanionServer(("127.0.0.1", 0), COMPANION.Snapshot(), "test-read-only-token", 180)
        thread = threading.Thread(target=server.serve_forever, daemon=True); thread.start()
        opener = build_opener(ProxyHandler({}))
        try:
            code = server.begin_pairing()
            request = Request(f"http://127.0.0.1:{server.server_port}/v1/pair", data=json.dumps({"code":code}).encode(),
                              headers={"Content-Type":"application/json"})
            with opener.open(request, timeout=3) as response:
                self.assertEqual(json.load(response), {"token":"test-read-only-token"})
            with self.assertRaises(HTTPError): opener.open(request, timeout=3)
        finally:
            server.shutdown(); server.server_close(); thread.join(timeout=3)

    def test_unknown_is_distinct_from_zero(self):
        empty = COMPANION.normalize({"rateLimits": {}})
        self.assertIsNone(empty["primary"])
        self.assertIsNone(empty["credits"])
        self.assertIsNone(empty["resets"])
        zero = COMPANION.normalize({"rateLimits": {"primary": {"usedPercent": 0, "resetsAt": 0}},
                                    "rateLimitResetCredits": {"availableCount": 0, "credits": []}})
        self.assertEqual(zero["primary"]["remaining_percent"], 100)
        self.assertEqual(zero["resets"]["available_count"], 0)
        self.assertEqual(zero["resets"]["expires_at"], [])
        self.assertTrue(zero["resets"]["details_complete"])

    def test_prefers_codex_bucket_and_filters_private_fields(self):
        result = COMPANION.normalize({
            "rateLimits": {"primary": {"usedPercent": 99}},
            "rateLimitsByLimitId": {"codex": {"primary": {"usedPercent": 25, "windowDurationMins": 300, "resetsAt": 123},
                                                          "credits": {"balance": "12.50", "unlimited": False, "private": "secret"}}},
            "rateLimitResetCredits": {"availableCount": 6, "credits": [{"id": "secret", "status": "available", "expiresAt": 456}]},
            "account": {"email": "private@example.invalid"},
        })
        self.assertEqual(result["primary"]["remaining_percent"], 75)
        self.assertEqual(result["credits"]["balance"], "12.50")
        self.assertEqual(result["resets"]["available_count"], 6)
        self.assertFalse(result["resets"]["details_complete"])
        self.assertNotIn("secret", json.dumps(result))
        self.assertNotIn("email", json.dumps(result))

    def test_rejects_malformed_windows(self):
        for value in [True, "25", -1, 101, 10 ** 1000, float("nan"), float("inf")]:
            self.assertIsNone(COMPANION.window({"usedPercent": value}))
        with self.assertRaises(COMPANION.CollectionError):
            COMPANION.normalize({"rateLimits": None})

    def test_failure_preserves_last_success_time_and_data(self):
        snapshot = COMPANION.Snapshot()
        self.assertEqual(snapshot.payload()["status"], "unavailable")
        snapshot.success({"primary": None})
        before = snapshot.payload()
        snapshot.failure("account_read_failed")
        after = snapshot.payload()
        self.assertEqual(after["status"], "stale")
        self.assertEqual(before["updated_at"], after["updated_at"])
        self.assertEqual(before["data"], after["data"])
        snapshot.success({"primary": None})
        self.assertEqual(snapshot.payload()["status"], "ok")
        snapshot.updated_monotonic = time.monotonic() - 1000
        self.assertEqual(snapshot.payload()["status"], "stale")

    def test_http_requires_token_and_exposes_only_read_route(self):
        server = COMPANION.CompanionServer(("127.0.0.1", 0), COMPANION.Snapshot(), "test-pairing-token", 180)
        worker = threading.Thread(target=server.serve_forever, daemon=True)
        worker.start()
        base = f"http://127.0.0.1:{server.server_port}"
        try:
            opener = build_opener(ProxyHandler({}))
            for token, path, expected in [(None, "/v1/pc", 401), ("wrong", "/v1/pc", 401), (None, "/v1/codex", 401), ("wrong", "/v1/codex", 401),
                                          ("test-pairing-token", "/v1/consume", 404)]:
                request = Request(base + path, headers={} if token is None else {"Authorization": "Bearer " + token})
                with self.assertRaises(HTTPError) as error:
                    opener.open(request, timeout=3)
                self.assertEqual(error.exception.code, expected)
            request = Request(base + "/v1/codex", headers={"Authorization": "Bearer test-pairing-token"})
            with opener.open(request, timeout=3) as response:
                self.assertEqual(json.load(response)["status"], "unavailable")
            request = Request(base + "/v1/pc", headers={"Authorization": "Bearer test-pairing-token"})
            with opener.open(request, timeout=3) as response:
                payload = json.load(response)
                self.assertEqual(payload['schema_version'],1)
                self.assertNotIn('token',payload)
            request = Request(base + "/v1/codex", method="POST", data=b"{}")
            with self.assertRaises(HTTPError) as error:
                opener.open(request, timeout=3)
            self.assertEqual(error.exception.code, 501)
        finally:
            server.shutdown()
            server.server_close()
            worker.join(timeout=3)


if __name__ == "__main__":
    unittest.main()
