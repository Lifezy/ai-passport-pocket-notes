#!/usr/bin/env python3
"""Desktop launcher for the read-only Codex companion, with copyable pairing."""
from pathlib import Path
import os
import shutil
import socket
import threading
import tkinter as tk
from tkinter import ttk
from urllib.request import getproxies

from codex_companion import CollectionError, CompanionServer, Snapshot, collect, token_from_file


def executable():
    found = shutil.which("codex")
    if found:
        return found
    base = Path(os.environ.get("LOCALAPPDATA", "")) / "OpenAI/Codex/bin"
    choices = list(base.glob("*/codex.exe"))
    return str(max(choices, key=lambda path: path.stat().st_mtime)) if choices else None


def lan_address():
    # Route selection only; UDP connect does not send a packet to the NAS.
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as connection:
        connection.connect(("192.168.1.250", 9))
        return connection.getsockname()[0]


class Assistant:
    def __init__(self, root):
        self.root = root
        self.server = None
        self.threads = []
        self.stop = threading.Event()
        self.closing = False
        self.pairing = ""
        self.address = ""
        self.snapshot = Snapshot()
        root.title("AI Passport · Codex 额度助手")
        root.geometry("570x420")
        root.minsize(550, 420)
        frame = ttk.Frame(root, padding=24); frame.pack(fill="both", expand=True)
        ttk.Label(frame, text="同步额度与电脑资源", font=("Microsoft YaHei UI", 16)).pack(anchor="w")
        ttk.Label(frame, text="复用电脑上已登录的 Codex。助手运行时会自动更新，\n不会提交任务或兑换重置卡。", wraplength=510).pack(anchor="w", pady=16)
        self.status = tk.StringVar(root, "先点击启动，再把电脑地址和六位配对码填入设备的手机设置页。")
        self.start_button = ttk.Button(frame, text="启动额度助手", command=self.start); self.start_button.pack(fill="x", pady=6)
        self.copy_button = ttk.Button(frame, text="生成新的六位配对码", command=self.copy, state="disabled"); self.copy_button.pack(fill="x", pady=6)
        self.pair_label = tk.StringVar(root, "")
        ttk.Label(frame, textvariable=self.pair_label, font=("Microsoft YaHei UI", 14)).pack(anchor="w", pady=10)
        ttk.Label(frame, textvariable=self.status, wraplength=510).pack(anchor="w", pady=16)
        ttk.Label(frame, text="请保持窗口打开。关闭后，设备会保留最后一次额度数据。", wraplength=510).pack(anchor="w")
        root.protocol("WM_DELETE_WINDOW", self.close)
        root.after(500, self.tick)

    def start(self):
        if self.server:
            return
        codex = executable()
        try:
            address = lan_address()
            token = token_from_file(Path(__file__).resolve().parents[1] / "build/private/companion.token")
            self.server = CompanionServer((address, 8765), self.snapshot, token, 180)
        except (OSError, ValueError):
            self.status.set("启动失败。请确认电脑已连接家庭网络，或已有另一个额度助手正在运行。")
            return
        self.pairing = f"http://{address}:8765|{token}"
        self.address = address
        proxy = getproxies().get("https") or getproxies().get("http")

        def poll():
            while not self.stop.is_set():
                try:
                    if codex:
                        self.snapshot.success(collect(codex, proxy))
                    else:
                        self.snapshot.failure("codex_not_installed")
                except (CollectionError, OSError):
                    self.snapshot.failure("collection_unavailable")
                self.stop.wait(60)

        self.threads = [threading.Thread(target=self.server.serve_forever, daemon=True),
                        threading.Thread(target=poll, daemon=True)]
        for thread in self.threads: thread.start()
        self.start_button.configure(state="disabled")
        self.copy_button.configure(state="normal")
        self.copy()

    def copy(self):
        if not self.pairing:
            return
        code = self.server.begin_pairing()
        self.pair_label.set(f"电脑地址：{self.address}\n配对码：{code}（两分钟有效）")
        self.status.set("在手机的 Codex 设置卡片填写上方地址与配对码，点击配对即可。")

    def tick(self):
        if self.closing:
            if not any(thread.is_alive() for thread in self.threads):
                if self.server: self.server.server_close()
                self.pairing = ""; self.root.destroy(); return
        elif self.server:
            payload = self.snapshot.payload()
            if payload["status"] == "ok" and payload["sample_age_seconds"] is not None and payload["sample_age_seconds"] < 2:
                self.status.set("额度读取成功，正在自动同步。首次接入请在手机填写上方地址与配对码。")
            elif payload["status"] in ("stale", "unavailable") and payload["error"]:
                self.status.set("暂时无法读取额度，请确认 Codex 登录和代理。电脑监控仍可使用，助手会自动重试额度。")
        self.root.after(500, self.tick)

    def close(self):
        if self.closing:
            return
        self.closing = True; self.stop.set()
        self.start_button.configure(state="disabled"); self.copy_button.configure(state="disabled")
        if self.server:
            self.status.set("正在停止助手，请稍候……")
            thread = threading.Thread(target=self.server.shutdown, daemon=True)
            self.threads.append(thread); thread.start()
        else:
            self.root.destroy()


def main():
    root = tk.Tk(); Assistant(root); root.mainloop()


if __name__ == "__main__":
    main()
