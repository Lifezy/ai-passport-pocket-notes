import sys
from pathlib import Path
import unittest
from unittest.mock import patch
import subprocess
from types import SimpleNamespace
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from pc_metrics import PcMonitor, cpu_percent, gpu_values, NvidiaGpuCounters, GPU_UNKNOWN, GPU_EXTRA, extended_gpu_values


class MetricsTests(unittest.TestCase):
    def test_gpu_values_unknown_independent_fields_and_bounds(self):
        values = gpu_values('50, 62, 8192, 4096\n')
        self.assertEqual(values, {'gpu_percent':50, 'gpu_temperature_c':62, 'gpu_memory_used_percent':50})
        self.assertEqual(gpu_values('0, N/A, 8192, 0\n'),
                         {'gpu_percent':0, 'gpu_temperature_c':None, 'gpu_memory_used_percent':0})
        for output in ['', '50,62', '1,2,3,4\n1,2,3,4', 'NaN, -1, 0, 10', '101, 151, 1, 2', 'x'*4097]:
            self.assertEqual(gpu_values(output), GPU_UNKNOWN)

    def test_gpu_query_is_read_only_hidden_and_bounded(self):
        gpu = NvidiaGpuCounters('driver/nvidia-smi.exe')
        with patch('pc_metrics.subprocess.run', return_value=SimpleNamespace(returncode=0, stdout='1, 40, 8192, 2048, 50, 30, 1500')) as run:
            self.assertEqual(gpu.read()['gpu_temperature_c'],40)
        arguments, options = run.call_args
        self.assertIn('--id=0', arguments[0])
        self.assertTrue(any(value.startswith('--query-gpu=') for value in arguments[0]))
        self.assertEqual(options['timeout'],2)
        self.assertNotIn('shell',options)
        self.assertIn('creationflags',options)
        for failure in [OSError(), subprocess.TimeoutExpired('query',2)]:
            with patch('pc_metrics.subprocess.run', side_effect=failure):
                self.assertEqual(gpu.read(),dict(GPU_UNKNOWN,**GPU_EXTRA))
        with patch('pc_metrics.subprocess.run', return_value=SimpleNamespace(returncode=1, stdout='driver failure')):
            self.assertEqual(gpu.read(),dict(GPU_UNKNOWN,**GPU_EXTRA))

    def test_gpu_failure_does_not_mark_cpu_stale_or_reuse_temperature(self):
        class Counters:
            samples=0
            def times(self):
                self.samples+=1
                return (self.samples*10,self.samples*30,self.samples*20)
            def read(self):
                return {'memory_used_percent':50,'system_disk_total_bytes':1000,'system_disk_free_bytes':400,'uptime_seconds':60}
        gpu=SimpleNamespace(read=lambda:gpu_values('10, 60, 1000, 500'))
        monitor=PcMonitor(Counters(),gpu)
        first=monitor.payload()
        self.assertEqual(first['data']['gpu_temperature_c'],60)
        gpu.read=lambda:dict(GPU_UNKNOWN)
        with patch('pc_metrics.time.monotonic',return_value=monitor.last_sample+5):
            current=monitor.payload()
        self.assertEqual(current['status'],'ok')
        self.assertIsNone(current['data']['gpu_temperature_c'])
        self.assertEqual(current['data']['memory_used_percent'],50)

    def test_extended_gpu_and_native_counter_validation(self):
        value=extended_gpu_values('50,62,8192,4096,123.4,40,1800')
        self.assertEqual(value['gpu_memory_free_bytes'],4096*1048576)
        self.assertEqual(value['gpu_power_w'],123.4)
        value=extended_gpu_values('0,N/A,8192,0,N/A,0,NaN')
        self.assertIsNone(value['gpu_power_w']);self.assertEqual(value['gpu_fan_percent'],0)
        from pc_performance import valid_value
        self.assertEqual(valid_value(0,0),0);self.assertEqual(valid_value(1,25),25)
        for status,value in [(2,10),(0,-1),(0,float('nan')),(1,float('inf'))]:self.assertIsNone(valid_value(status,value))

    def test_windows_idle_is_already_in_kernel(self):
        self.assertEqual(cpu_percent((100,200,100),(120,240,120)), 100 * 40 / 60)
        self.assertIsNone(cpu_percent(None,(1,2,3)))
        for before, after in [((1,2,3),(1,2,3)),((10,20,30),(1,2,3)),((0,0,0),(100,1,1))]:
            self.assertIsNone(cpu_percent(before,after))

    def test_stale_snapshot_retains_original_values_and_time(self):
        class Counters:
            fail = False
            samples = 0
            def times(self):
                self.samples += 1
                if self.fail: raise OSError()
                return (self.samples * 10,self.samples * 30,self.samples * 20)
            def read(self):
                return {'memory_used_percent':50,'system_disk_total_bytes':1000,'system_disk_free_bytes':400,'uptime_seconds':60}
        counters=Counters(); monitor=PcMonitor(counters)
        first=monitor.payload(); self.assertEqual(first['status'],'ok')
        self.assertEqual(first['data']['cpu_percent'],80)
        again=monitor.payload(); self.assertEqual(counters.samples,2)
        with patch('pc_metrics.time.monotonic',return_value=monitor.last_sample+5):
            counters.fail=True; stale=monitor.payload()
        self.assertEqual(stale['status'],'stale')
        self.assertEqual(stale['updated_at'],first['updated_at'])
        self.assertEqual(stale['data'],first['data'])
        first['data']['cpu_percent']=0
        self.assertEqual(again['data']['cpu_percent'],80)


if __name__=='__main__': unittest.main()
