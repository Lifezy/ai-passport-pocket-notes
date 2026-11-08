#!/usr/bin/env python3
"""Chinese desktop Wi-Fi setup for the already-installed Pocket Notes firmware."""
from __future__ import annotations

import queue
import threading
import tkinter as tk
from tkinter import font, ttk

from configure_pocket import send_settings, validate_settings


def preferred_port(ports) -> str:
    matches = [port.device for port in ports if port.vid == 0x303A and port.pid == 0x1001]
    return matches[0] if len(matches) == 1 else ""


class WifiSetup:
    def __init__(self, root):
        self.root = root
        self.busy = False
        self.results = queue.Queue()
        self.devices = {}
        self.port = tk.StringVar(root)
        self.ssid = tk.StringVar(root)
        self.password = tk.StringVar(root)
        self.visible = tk.BooleanVar(root, False)
        self.status = tk.StringVar(root, "连接设备后，选择串口并填写家里的 2.4 GHz Wi-Fi。")
        root.title("AI Passport · Wi-Fi 设置")
        root.geometry("580x450")
        root.minsize(560, 450)
        if "Microsoft YaHei UI" in font.families(root):
            for name in ("TkDefaultFont", "TkTextFont", "TkMenuFont"):
                font.nametofont(name).configure(family="Microsoft YaHei UI", size=10)
        frame = ttk.Frame(root, padding=22)
        frame.pack(fill="both", expand=True)
        frame.columnconfigure(1, weight=1)
        ttk.Label(frame, text="连接家里的 Wi-Fi", font=("Microsoft YaHei UI", 16)).grid(
            row=0, column=0, columnspan=3, sticky="w", pady=(0, 8))
        ttk.Label(frame, text="首次使用需先刷入多页便笺固件。此窗口只设置网络。", wraplength=520).grid(
            row=1, column=0, columnspan=3, sticky="w", pady=(0, 18))
        ttk.Label(frame, text="设备串口").grid(row=2, column=0, sticky="w", padx=(0, 14))
        self.port_box = ttk.Combobox(frame, textvariable=self.port, state="readonly")
        self.port_box.grid(row=2, column=1, sticky="ew", pady=6)
        self.refresh_button = ttk.Button(frame, text="刷新", command=self.refresh)
        self.refresh_button.grid(row=2, column=2, padx=(10, 0))
        ttk.Label(frame, text="Wi-Fi 名称").grid(row=3, column=0, sticky="w")
        self.ssid_entry = ttk.Entry(frame, textvariable=self.ssid)
        self.ssid_entry.grid(row=3, column=1, columnspan=2, sticky="ew", pady=6)
        ttk.Label(frame, text="Wi-Fi 密码").grid(row=4, column=0, sticky="w")
        self.password_entry = ttk.Entry(frame, textvariable=self.password, show="•")
        self.password_entry.grid(row=4, column=1, columnspan=2, sticky="ew", pady=6)
        self.show_button = ttk.Checkbutton(frame, text="显示密码", variable=self.visible,
            command=lambda: self.password_entry.configure(show="" if self.visible.get() else "•"))
        self.show_button.grid(row=5, column=1, sticky="w", pady=(0, 10))
        self.save_button = ttk.Button(frame, text="保存到设备", command=self.save)
        self.save_button.grid(row=6, column=1, columnspan=2, sticky="ew", pady=6)
        ttk.Label(frame, textvariable=self.status, wraplength=520).grid(
            row=7, column=0, columnspan=3, sticky="w", pady=(14, 0))
        root.protocol("WM_DELETE_WINDOW", self.close)
        self.refresh()
        root.after(100, self.poll)

    def refresh(self):
        if self.busy:
            return
        try:
            from serial.tools.list_ports import comports
            ports = list(comports())
        except (ImportError, OSError):
            self.status.set("未找到串口组件，请使用项目的 WiFi-Setup.cmd 打开窗口。")
            return
        self.devices = {f"{port.device}  ·  {port.description}": port.device for port in ports}
        self.port_box.configure(values=list(self.devices))
        selected = preferred_port(ports)
        current = self.devices.get(self.port.get())
        if not current:
            self.port.set(next((label for label, device in self.devices.items() if device == selected), ""))
        if not ports:
            self.status.set("尚未发现串口。请打开设备电源，用数据线连接电脑，再点击刷新。")

    def set_busy(self, busy):
        self.busy = busy
        for widget in (self.refresh_button, self.ssid_entry, self.password_entry, self.show_button, self.save_button):
            widget.configure(state="disabled" if busy else "normal")
        self.port_box.configure(state="disabled" if busy else "readonly")

    def save(self):
        if self.busy:
            return
        device = self.devices.get(self.port.get())
        if not device:
            self.status.set("请先选择 AI Passport 对应的设备串口。")
            return
        if not self.ssid.get():
            self.status.set("请填写 Wi-Fi 名称。")
            return
        try:
            payload = validate_settings({"wifi_ssid": self.ssid.get(), "wifi_password": self.password.get()})
        except ValueError:
            self.status.set("请检查名称和密码：名称最多 32 字节；密码为 8–63 字节或 64 位十六进制。开放网络可留空。")
            return
        self.set_busy(True)
        self.status.set("正在保存，请保持 USB 连接……")
        threading.Thread(target=self.write, args=(device, payload), daemon=True).start()

    def write(self, device, payload):
        try:
            send_settings(device, payload)
        except TimeoutError:
            self.results.put((False, "设备未响应。请确认已刷入多页便笺固件，并关闭占用串口的程序后重试。"))
        except ValueError:
            self.results.put((False, "设备未接受设置，原设置已保留。请检查名称和密码。"))
        except Exception:
            # Serial exceptions can contain private data: show no raw exception.
            self.results.put((False, "无法写入设备。请检查串口、数据线及串口是否被其他程序占用。"))
        else:
            self.results.put((True, "设置已保存，设备正在重启。请查看设备屏幕，确认 Wi-Fi 是否连接成功。"))

    def poll(self):
        try:
            success, message = self.results.get_nowait()
        except queue.Empty:
            pass
        else:
            self.set_busy(False)
            self.status.set(message)
            if success:
                self.password.set("")
                self.visible.set(False)
                self.password_entry.configure(show="•")
        self.root.after(100, self.poll)

    def close(self):
        if self.busy:
            self.status.set("正在保存，请等待结果后关闭窗口。")
            return
        self.password.set("")
        self.root.destroy()


def main():
    root = tk.Tk()
    WifiSetup(root)
    root.mainloop()


if __name__ == "__main__":
    main()
