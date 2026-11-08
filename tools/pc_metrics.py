"""Read aggregate Windows counters only; no process, file or account enumeration."""
import ctypes
import csv
from ctypes import wintypes
import math
import os
from pathlib import Path
import subprocess
import threading
import time
from pc_performance import PerformanceCounters, UNKNOWN as PERFORMANCE_UNKNOWN


def cpu_percent(previous, current):
    if previous is None:
        return None
    idle, kernel, user = [now - before for now, before in zip(current, previous)]
    total = kernel + user  # Windows kernel time includes idle time.
    if total <= 0 or min(idle, kernel, user) < 0 or idle > total:
        return None
    return (total - idle) * 100.0 / total


class MemoryStatus(ctypes.Structure):
    _fields_ = [("length", wintypes.DWORD), ("load", wintypes.DWORD)] + [
        (name, ctypes.c_ulonglong) for name in
        ("total_phys", "avail_phys", "total_page", "avail_page", "total_virtual", "avail_virtual", "avail_extended")]


class WindowsCounters:
    def __init__(self):
        self.performance=None
        try:self.performance=PerformanceCounters()
        except (OSError,AttributeError):pass
        self.api = ctypes.WinDLL("kernel32", use_last_error=True)
        self.api.GetSystemTimes.argtypes = [ctypes.POINTER(wintypes.FILETIME)] * 3
        self.api.GetSystemTimes.restype = wintypes.BOOL
        self.api.GlobalMemoryStatusEx.argtypes = [ctypes.POINTER(MemoryStatus)]
        self.api.GlobalMemoryStatusEx.restype = wintypes.BOOL
        self.api.GetDiskFreeSpaceExW.argtypes = [wintypes.LPCWSTR] + [ctypes.POINTER(ctypes.c_ulonglong)] * 3
        self.api.GetDiskFreeSpaceExW.restype = wintypes.BOOL
        self.api.GetTickCount64.argtypes = []
        self.api.GetTickCount64.restype = ctypes.c_ulonglong

    def times(self):
        values = [wintypes.FILETIME() for _ in range(3)]
        if not self.api.GetSystemTimes(*(ctypes.byref(value) for value in values)):
            raise OSError("CPU counters unavailable")
        return tuple((value.dwHighDateTime << 32) | value.dwLowDateTime for value in values)

    def read(self):
        memory = MemoryStatus(); memory.length = ctypes.sizeof(memory)
        if not self.api.GlobalMemoryStatusEx(ctypes.byref(memory)):
            raise OSError("Memory counters unavailable")
        free, total, total_free = (ctypes.c_ulonglong() for _ in range(3))
        drive = os.environ.get("SystemDrive", "C:") + "\\"
        if not self.api.GetDiskFreeSpaceExW(drive, ctypes.byref(free), ctypes.byref(total), ctypes.byref(total_free)):
            raise OSError("Disk counters unavailable")
        data = {"memory_used_percent": (memory.total_phys - memory.avail_phys) * 100.0 / memory.total_phys,
                "system_disk_total_bytes": total.value, "system_disk_free_bytes": free.value,
                "uptime_seconds": self.api.GetTickCount64() // 1000,
                "memory_used_bytes":memory.total_phys-memory.avail_phys,"memory_free_bytes":memory.avail_phys,"memory_total_bytes":memory.total_phys}
        try:data.update(self.performance.read() if self.performance else PERFORMANCE_UNKNOWN)
        except (OSError,ValueError):data.update(PERFORMANCE_UNKNOWN)
        return data

    def close(self):
        if self.performance:self.performance.close()


GPU_UNKNOWN = {"gpu_percent": None, "gpu_temperature_c": None, "gpu_memory_used_percent": None}


def gpu_values(output):
    """One NVIDIA GPU, in driver index order. Unsupported fields stay unknown."""
    result = dict(GPU_UNKNOWN)
    if not isinstance(output, str) or len(output) > 4096:
        return result
    rows = list(csv.reader(output.splitlines()))
    if len(rows) != 1 or len(rows[0]) != 4:
        return result

    def numeric(text, maximum):
        try:
            value = float(text.strip())
            return value if math.isfinite(value) and 0 <= value <= maximum else None
        except (ValueError, OverflowError):
            return None

    load, temperature, total, used = rows[0]
    result["gpu_percent"] = numeric(load, 100)
    result["gpu_temperature_c"] = numeric(temperature, 150)
    total, used = numeric(total, 1048576), numeric(used, 1048576)  # Driver units: MiB.
    if total and used is not None and used <= total:
        result["gpu_memory_used_percent"] = used * 100.0 / total
    return result


GPU_EXTRA = {key:None for key in ('gpu_memory_used_bytes','gpu_memory_free_bytes','gpu_memory_total_bytes','gpu_power_w','gpu_fan_percent','gpu_clock_mhz')}
def extended_gpu_values(output):
    result=dict(GPU_UNKNOWN,**GPU_EXTRA)
    if not isinstance(output,str) or len(output)>4096:return result
    rows=list(csv.reader(output.splitlines()))
    if len(rows)!=1 or len(rows[0])!=7:return result
    row=rows[0];result.update(gpu_values(','.join(row[:4])))
    def number(text,upper):
        try:
            value=float(text.strip());return value if math.isfinite(value) and 0<=value<=upper else None
        except (ValueError,OverflowError):return None
    total,used=number(row[2],1048576),number(row[3],1048576)
    if total is not None and used is not None and used<=total:
        result.update(gpu_memory_total_bytes=total*1048576,gpu_memory_used_bytes=used*1048576,gpu_memory_free_bytes=(total-used)*1048576)
    for key,text,upper in zip(('gpu_power_w','gpu_fan_percent','gpu_clock_mhz'),row[4:],(5000,100,20000)):
        result[key]=number(text,upper)
    return result


class NvidiaGpuCounters:
    """Read-only driver query in a bounded hidden subprocess; no new dependency."""
    def __init__(self, executable):
        self.executable = executable

    @classmethod
    def discover(cls):
        if os.name != "nt":
            return None
        candidates = [Path(os.environ.get("SystemRoot", r"C:\Windows")) / "System32/nvidia-smi.exe",
                      Path(os.environ.get("ProgramW6432", r"C:\Program Files")) / "NVIDIA Corporation/NVSMI/nvidia-smi.exe"]
        return next((cls(str(path)) for path in candidates if path.is_absolute() and path.is_file()), None)

    def read(self):
        try:
            response = subprocess.run(
                [self.executable, "--id=0", "--query-gpu=utilization.gpu,temperature.gpu,memory.total,memory.used,power.draw,fan.speed,clocks.current.graphics",
                 "--format=csv,noheader,nounits"], stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                stderr=subprocess.DEVNULL, text=True, encoding="ascii", errors="replace", timeout=2,
                check=False, creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
            return extended_gpu_values(response.stdout) if response.returncode == 0 else dict(GPU_UNKNOWN,**GPU_EXTRA)
        except (OSError, subprocess.TimeoutExpired, ValueError, csv.Error):
            return dict(GPU_UNKNOWN,**GPU_EXTRA)


class PcMonitor:
    def __init__(self, counters=None, gpu=None):
        self.lock = threading.Lock(); self.data = None; self.updated_at = None
        self.last_sample = None; self.previous = None
        self.counters = counters
        self.gpu = gpu if counters is not None or gpu is not None else NvidiaGpuCounters.discover()
        if self.counters is None and os.name == "nt":
            try:
                self.counters = WindowsCounters()
            except (OSError, AttributeError):
                pass
        if self.counters:
            try: self.previous = self.counters.times()
            except OSError: pass

    def payload(self):
        with self.lock:
            if self.counters is None:
                return {"schema_version":1, "status":"unavailable", "updated_at":None, "data":None}
            try:
                # Coalesce simultaneous UI clients instead of reporting near-zero CPU intervals.
                now = time.monotonic()
                if self.last_sample is None or now - self.last_sample >= 1:
                    current = self.counters.times()
                    data = self.counters.read()
                    data["cpu_percent"] = cpu_percent(self.previous, current)
                    # GPU failure never discards CPU/RAM or reuses an old temperature.
                    data.update(self.gpu.read() if self.gpu else dict(GPU_UNKNOWN,**GPU_EXTRA))
                    if not all(value is None or isinstance(value, (int, float)) and math.isfinite(value) and value >= 0 for value in data.values()):
                        raise ValueError("Invalid counters")
                    self.previous = current; self.last_sample = now
                    self.data = data; self.updated_at = int(time.time())
                status = "ok"
            except (OSError, ValueError, ZeroDivisionError):
                status = "stale" if self.data is not None else "unavailable"
            return {"schema_version":1, "status":status, "updated_at":self.updated_at,
                    "data":None if self.data is None else dict(self.data)}

    def close(self):
        with self.lock:
            if self.counters and hasattr(self.counters,'close'):self.counters.close()
