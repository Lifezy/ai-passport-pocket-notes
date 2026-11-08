#!/usr/bin/env python3
"""Read-only Codex allowance companion; no third-party Python dependencies."""

from __future__ import annotations

import argparse
import hmac
import json
import math
import os
from pathlib import Path
import queue
import secrets
import shutil
import subprocess
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Any
from pc_metrics import PcMonitor

MAX_RPC_BYTES = 1024 * 1024


class CollectionError(Exception):
    """An intentionally non-sensitive collection error."""


def number(value: Any) -> float | None:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return None
    try:
        return float(value) if math.isfinite(value) else None
    except OverflowError:
        return None


def timestamp(value: Any) -> int | None:
    parsed = number(value)
    return int(parsed) if parsed is not None and parsed >= 0 else None


def window(value: Any) -> dict | None:
    if not isinstance(value, dict):
        return None
    used = number(value.get("usedPercent"))
    if used is None or not 0 <= used <= 100:
        return None
    minutes = number(value.get("windowDurationMins"))
    return {
        "used_percent": used,
        "remaining_percent": 100 - used,
        "window_minutes": minutes if minutes is not None and minutes > 0 else None,
        "resets_at": timestamp(value.get("resetsAt")),
    }


def normalize(result: dict) -> dict:
    """Whitelist display fields, retaining unknown/null independently from zero."""
    buckets = result.get("rateLimitsByLimitId")
    bucket = buckets.get("codex") if isinstance(buckets, dict) else None
    if not isinstance(bucket, dict):
        bucket = result.get("rateLimits")
    if not isinstance(bucket, dict):
        raise CollectionError("unsupported_response")
    raw_credits = bucket.get("credits")
    credits = None
    if isinstance(raw_credits, dict):
        balance = raw_credits.get("balance")
        if not isinstance(balance, str) or len(balance) > 64:
            balance = None
        credits = {
            "balance": balance,
            "has_credits": raw_credits.get("hasCredits") if isinstance(raw_credits.get("hasCredits"), bool) else None,
            "unlimited": raw_credits.get("unlimited") if isinstance(raw_credits.get("unlimited"), bool) else None,
        }
    raw_resets = result.get("rateLimitResetCredits")
    resets = None
    if isinstance(raw_resets, dict):
        count = raw_resets.get("availableCount")
        if isinstance(count, bool) or not isinstance(count, int) or count < 0:
            count = None
        details = raw_resets.get("credits")
        expiry = None
        if isinstance(details, list):
            expiry = [timestamp(row.get("expiresAt")) for row in details[:32]
                      if isinstance(row, dict) and row.get("status") == "available"]
        resets = {"available_count": count, "expires_at": expiry,
                  "details_complete": count is not None and expiry is not None and len(expiry) == count}
    return {
        "primary": window(bucket.get("primary")),
        "secondary": window(bucket.get("secondary")),
        "credits": credits,
        "resets": resets,
    }


class AppServer:
    """Short-lived stdio connection with bounded reads and an RPC deadline."""

    def __init__(self, executable: str, proxy: str | None = None, timeout: float = 25):
        self.timeout = timeout
        self.messages: queue.Queue = queue.Queue(maxsize=8)
        self.closed = threading.Event()
        environment = os.environ.copy()
        if proxy:
            environment.update(HTTP_PROXY=proxy, HTTPS_PROXY=proxy, ALL_PROXY=proxy)
        self.process = subprocess.Popen(
            [executable, "app-server"], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL, env=environment,
            creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0,
        )
        self.reader = threading.Thread(target=self._read, daemon=True)
        self.reader.start()

    def _read(self):
        try:
            while not self.closed.is_set():
                line = self.process.stdout.readline(MAX_RPC_BYTES + 1)
                if not line:
                    break
                if len(line) > MAX_RPC_BYTES:
                    break
                message = json.loads(line)
                # Notifications contain no request ID and need no buffering.
                if isinstance(message, dict) and "id" in message:
                    self.messages.put(message, timeout=0.1)
        except (ValueError, OSError, queue.Full):
            pass
        finally:
            try:
                self.messages.put_nowait(None)
            except queue.Full:
                pass

    def send(self, message: dict):
        try:
            self.process.stdin.write(json.dumps(message).encode("utf-8") + b"\n")
            self.process.stdin.flush()
        except (OSError, ValueError):
            raise CollectionError("app_server_closed") from None

    def request(self, identifier: int, method: str, params: dict | None = None):
        self.send({"id": identifier, "method": method, "params": params or {}})
        deadline = time.monotonic() + self.timeout
        while True:
            if time.monotonic() >= deadline:
                raise CollectionError("app_server_timeout")
            try:
                message = self.messages.get(timeout=max(0, deadline - time.monotonic()))
            except queue.Empty:
                raise CollectionError("app_server_timeout") from None
            if message is None:
                raise CollectionError("app_server_closed")
            if "method" in message:
                # This read-only client cannot approve tools or mutate accounts.
                self.send({"id": message["id"], "error": {"code": -32601, "message": "Read-only client"}})
                continue
            if message.get("id") != identifier:
                continue
            if "error" in message:
                raise CollectionError("account_read_failed")
            result = message.get("result")
            if not isinstance(result, dict):
                raise CollectionError("unsupported_response")
            return result

    def close(self):
        self.closed.set()
        self.process.stdin.close()
        if self.process.poll() is None:
            self.process.terminate()
        try:
            self.process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait(timeout=3)
        self.reader.join(timeout=1)
        self.process.stdout.close()


def collect(executable: str, proxy: str | None = None) -> dict:
    server = AppServer(executable, proxy)
    try:
        server.request(1, "initialize", {"clientInfo": {
            "name": "ai_passport_allowance", "title": "AI Passport Allowance", "version": "0.1.0",
        }})
        server.send({"method": "initialized", "params": {}})
        return normalize(server.request(2, "account/rateLimits/read"))
    finally:
        server.close()


class Snapshot:
    def __init__(self):
        self.lock = threading.Lock()
        self.data: dict | None = None
        self.updated_at: int | None = None
        self.updated_monotonic: float | None = None
        self.error: str | None = None

    def success(self, data: dict):
        with self.lock:
            self.data = data
            self.updated_at = int(time.time())
            self.updated_monotonic = time.monotonic()
            self.error = None

    def failure(self, error: str):
        with self.lock:
            self.error = error

    def payload(self, stale_after: float = 180) -> dict:
        with self.lock:
            age = None if self.updated_monotonic is None else max(0, int(time.monotonic() - self.updated_monotonic))
            status = "unavailable" if self.data is None else "stale" if self.error or age > stale_after else "ok"
            return {"schema_version": 1, "status": status, "updated_at": self.updated_at,
                    "sample_age_seconds": age, "error": self.error, "data": self.data}


class CompanionServer(ThreadingHTTPServer):
    daemon_threads = True
    request_queue_size = 4

    def __init__(self, address, snapshot: Snapshot, token: str, stale_after: float):
        self.snapshot = snapshot
        self.token = token
        self.stale_after = stale_after
        self.pc_monitor = PcMonitor()
        self.pairing_lock = threading.Lock()
        self.pairing_code = None
        self.pairing_deadline = 0
        self.pairing_attempts = 0
        super().__init__(address, CompanionHandler)

    def server_close(self):
        super().server_close()
        self.pc_monitor.close()

    def begin_pairing(self):
        with self.pairing_lock:
            self.pairing_code = f"{secrets.randbelow(1000000):06d}"
            self.pairing_deadline = time.monotonic() + 120
            self.pairing_attempts = 0
            return self.pairing_code

    def redeem_pairing(self, code):
        with self.pairing_lock:
            if self.pairing_code is None or time.monotonic() >= self.pairing_deadline or self.pairing_attempts >= 5:
                return None
            self.pairing_attempts += 1
            if not isinstance(code, str) or not hmac.compare_digest(code.encode(), self.pairing_code.encode()):
                return None
            self.pairing_code = None
            return self.token


class CompanionHandler(BaseHTTPRequestHandler):
    def setup(self):
        super().setup()
        self.connection.settimeout(5)

    def log_message(self, *_):
        pass

    def do_POST(self):
        if self.path != "/v1/pair" or self.server.pairing_code is None:
            self.send_error(501)
            return
        try:
            length = int(self.headers.get("Content-Length", "0"))
            if not 1 <= length <= 128 or self.headers.get("Content-Type") != "application/json":
                raise ValueError
            values = json.loads(self.rfile.read(length))
            code = values.get("code") if isinstance(values, dict) else None
            token = self.server.redeem_pairing(code)
        except (ValueError, OSError, RecursionError):
            token = None
        if token is None:
            self.send_error(403)
            return
        payload = json.dumps({"token": token}).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(payload)

    def do_GET(self):
        if not hmac.compare_digest(self.headers.get("Authorization", "").encode(),
                                   ("Bearer " + self.server.token).encode()):
            self.send_error(401)
            return
        if self.path not in ("/v1/codex", "/v1/pc"):
            self.send_error(404)
            return
        data = self.server.pc_monitor.payload() if self.path == "/v1/pc" else self.server.snapshot.payload(self.server.stale_after)
        payload = json.dumps(data,
                             ensure_ascii=True, allow_nan=False).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(payload)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(payload)


def token_from_file(path: Path) -> str:
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists():
        try:
            with path.open("x", encoding="ascii") as target:
                target.write(secrets.token_urlsafe(32) + "\n")
            if os.name != "nt":
                path.chmod(0o600)
        except FileExistsError:
            pass
    token = path.read_text(encoding="ascii").strip()
    if not 32 <= len(token) <= 128 or not all(c.isalnum() or c in "_-" for c in token):
        raise ValueError("Invalid companion pairing token file")
    return token


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--codex", default=shutil.which("codex"))
    parser.add_argument("--bind", default="127.0.0.1", help="Use the computer's LAN address for device access")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--proxy", help="Existing local HTTP/SOCKS proxy URL; inherited by the Codex child only")
    parser.add_argument("--interval", type=float, default=60)
    parser.add_argument("--token-file", type=Path, default=Path(__file__).resolve().parents[1] / "build/private/companion.token")
    args = parser.parse_args(argv)
    if not math.isfinite(args.interval) or args.interval < 30:
        parser.error("--interval must be at least 30 seconds")
    snapshot = Snapshot()
    stop = threading.Event()

    def poll():
        while not stop.is_set():
            try:
                if args.codex:
                    snapshot.success(collect(args.codex, args.proxy))
                else:
                    snapshot.failure("codex_not_installed")
            except CollectionError as error:
                snapshot.failure(str(error))
            except OSError:
                snapshot.failure("app_server_unavailable")
            stop.wait(args.interval)

    token = token_from_file(args.token_file)
    server = CompanionServer((args.bind, args.port), snapshot, token, max(180, args.interval * 3))
    worker = threading.Thread(target=poll, daemon=True)
    worker.start()
    print(f"Codex companion: http://{args.bind}:{args.port}/v1/codex")
    print("Pairing token is stored in the configured private file; account credentials stay in Codex.")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        stop.set()
        server.server_close()
        worker.join(timeout=32)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
