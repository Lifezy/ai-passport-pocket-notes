"""Optional native Windows PDH counters, with no sampling thread or process enumeration."""
import ctypes
import math
import os

UNKNOWN = {key:None for key in ('cpu_clock_mhz','disk_read_bps','disk_write_bps','net_rx_bps','net_tx_bps')}

class Formatted(ctypes.Structure):
    _fields_=[('status',ctypes.c_uint32),('value',ctypes.c_double)]

class Item(ctypes.Structure):
    _fields_=[('name',ctypes.c_wchar_p),('value',Formatted)]

def valid_value(status,value):
    return value if status in (0,1) and math.isfinite(value) and value>=0 else None

class PerformanceCounters:
    def __init__(self):
        if os.name!='nt': raise OSError('Windows counters unavailable')
        self.api=ctypes.WinDLL('pdh'); self.query=ctypes.c_void_p(); self.counters={}
        handle=ctypes.c_void_p; pointer=ctypes.POINTER
        signatures={
            'PdhOpenQueryW':([ctypes.c_wchar_p,ctypes.c_size_t,pointer(handle)],ctypes.c_int32),
            'PdhAddEnglishCounterW':([handle,ctypes.c_wchar_p,ctypes.c_size_t,pointer(handle)],ctypes.c_int32),
            'PdhCollectQueryData':([handle],ctypes.c_int32),
            'PdhGetFormattedCounterValue':([handle,ctypes.c_uint32,pointer(ctypes.c_uint32),pointer(Formatted)],ctypes.c_int32),
            'PdhGetFormattedCounterArrayW':([handle,ctypes.c_uint32,pointer(ctypes.c_uint32),pointer(ctypes.c_uint32),ctypes.c_void_p],ctypes.c_int32),
            'PdhCloseQuery':([handle],ctypes.c_int32)}
        for name,(args,result) in signatures.items():
            function=getattr(self.api,name);function.argtypes=args;function.restype=result
        if self.api.PdhOpenQueryW(None,0,ctypes.byref(self.query))!=0: raise OSError('Counters unavailable')
        paths={'cpu_clock_mhz':r'\Processor Information(_Total)\Processor Frequency',
               'disk_read_bps':r'\PhysicalDisk(_Total)\Disk Read Bytes/sec',
               'disk_write_bps':r'\PhysicalDisk(_Total)\Disk Write Bytes/sec',
               'net_rx_bps':r'\Network Interface(*)\Bytes Received/sec',
               'net_tx_bps':r'\Network Interface(*)\Bytes Sent/sec'}
        try:
            for key,path in paths.items():
                counter=handle()
                if self.api.PdhAddEnglishCounterW(self.query,path,0,ctypes.byref(counter))==0:self.counters[key]=counter
            self.api.PdhCollectQueryData(self.query)
        except BaseException:
            self.close();raise

    def read(self):
        result=dict(UNKNOWN)
        if not self.query or self.api.PdhCollectQueryData(self.query)!=0:return result
        for key,counter in self.counters.items():
            if key.startswith('net_'):
                size,count=ctypes.c_uint32(),ctypes.c_uint32()
                status=self.api.PdhGetFormattedCounterArrayW(counter,0x200,ctypes.byref(size),ctypes.byref(count),None)
                if status & 0xffffffff != 0x800007d2 or not 0<size.value<=262144:continue
                buffer=ctypes.create_string_buffer(size.value)
                if self.api.PdhGetFormattedCounterArrayW(counter,0x200,ctypes.byref(size),ctypes.byref(count),buffer)!=0:continue
                if count.value>1024 or count.value*ctypes.sizeof(Item)>size.value:continue
                items=ctypes.cast(buffer,ctypes.POINTER(Item));values=[]
                for i in range(count.value):
                    item=items[i]
                    if item.name and 'loopback' in item.name.lower():continue
                    value=valid_value(item.value.status,item.value.value)
                    if value is not None:values.append(value)
                if values:result[key]=sum(values)
            else:
                value=Formatted()
                if self.api.PdhGetFormattedCounterValue(counter,0x200,None,ctypes.byref(value))==0:result[key]=valid_value(value.status,value.value)
        return result

    def close(self):
        if self.query:
            self.api.PdhCloseQuery(self.query);self.query=ctypes.c_void_p()

    def __del__(self):
        try:self.close()
        except (AttributeError,OSError):pass
