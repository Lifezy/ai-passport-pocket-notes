#include "pocket_pc_display.h"
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <stdint.h>

static const char *const IDS[] = {"", "cpu", "gpu", "gpu_temp", "memory", "gpu_memory", "disk", "disk_free", "uptime", "status", "memory_used_bytes", "memory_free_bytes", "memory_total_bytes", "gpu_memory_used_bytes", "gpu_memory_free_bytes", "gpu_memory_total_bytes", "gpu_power_w", "gpu_fan_percent", "gpu_clock_mhz", "cpu_clock_mhz", "disk_read_bps", "disk_write_bps", "net_rx_bps", "net_tx_bps"};
static const char *const LABELS[] = {"", "CPU 占用", "GPU 占用", "GPU 温度", "内存占用", "显存占用", "系统盘占用", "可用空间", "开机时长", "连接状态", "已用内存", "可用内存", "总内存", "已用显存", "可用显存", "总显存", "GPU 功耗", "GPU 风扇", "GPU 频率", "CPU 频率", "磁盘读取", "磁盘写入", "网络接收", "网络发送"};
_Static_assert(sizeof(IDS) / sizeof(IDS[0]) == POCKET_PC_METRIC_COUNT, "Metric ID table");
_Static_assert(sizeof(LABELS) / sizeof(LABELS[0]) == POCKET_PC_METRIC_COUNT, "Metric label table");

void pocket_pc_display_default(pocket_pc_display_t *display) {
    *display = (pocket_pc_display_t){.metric = {POCKET_PC_CPU, POCKET_PC_GPU, POCKET_PC_GPU_TEMP,
        POCKET_PC_MEMORY, POCKET_PC_DISK, POCKET_PC_CONNECTION}, .count = 6};
}

bool pocket_pc_display_parse(const char *text, pocket_pc_display_t *display) {
    if (!text || !display || !text[0]) return false;
    size_t length = 0;
    while (length < POCKET_PC_DISPLAY_BYTES && text[length]) ++length;
    if (length == POCKET_PC_DISPLAY_BYTES) return false;
    pocket_pc_display_t next = {0};
    unsigned seen = 0;
    const char *start = text;
    while (*start) {
        const char *end = strchr(start, ',');
        size_t size = end ? (size_t)(end - start) : strlen(start);
        pocket_pc_metric_t metric = POCKET_PC_NONE;
        for (unsigned i = 1; i < POCKET_PC_METRIC_COUNT; ++i)
            if (strlen(IDS[i]) == size && !memcmp(start, IDS[i], size)) metric = (pocket_pc_metric_t)i;
        if (metric == POCKET_PC_NONE || next.count == POCKET_PC_DISPLAY_MAX || (seen & (1u << metric))) return false;
        seen |= 1u << metric;
        next.metric[next.count++] = metric;
        if (!end) break;
        start = end + 1;
        if (!*start) return false;
    }
    *display = next;
    return true;
}

bool pocket_pc_display_format(const pocket_pc_display_t *display, char *text, size_t capacity) {
    if (!display || !text || !capacity || !display->count || display->count > POCKET_PC_DISPLAY_MAX) return false;
    char result[POCKET_PC_DISPLAY_BYTES] = {0};
    size_t used = 0;
    unsigned seen = 0;
    for (unsigned i = 0; i < display->count; ++i) {
        unsigned metric = (unsigned)display->metric[i];
        if (!metric || metric >= POCKET_PC_METRIC_COUNT || (seen & (1u << metric))) return false;
        seen |= 1u << metric;
        size_t length = strlen(IDS[metric]);
        if (used + length + (i ? 1 : 0) >= sizeof(result)) return false;
        if (i) result[used++] = ',';
        memcpy(result + used, IDS[metric], length); used += length;
    }
    if (used >= capacity) return false;
    memcpy(text, result, used + 1);
    return true;
}

const char *pocket_pc_metric_label(pocket_pc_metric_t metric) {
    return metric > POCKET_PC_NONE && metric < POCKET_PC_METRIC_COUNT ? LABELS[metric] : "";
}

_Static_assert(POCKET_PC_METRIC_COUNT <= 32, "Metric bit mask capacity");
const char *pocket_pc_metric_key(pocket_pc_metric_t metric) {
    static const char *const keys[] = {"", "cpu_percent", "gpu_percent", "gpu_temperature_c", "memory_used_percent", "gpu_memory_used_percent", "", "system_disk_free_bytes", "uptime_seconds", "", "memory_used_bytes", "memory_free_bytes", "memory_total_bytes", "gpu_memory_used_bytes", "gpu_memory_free_bytes", "gpu_memory_total_bytes", "gpu_power_w", "gpu_fan_percent", "gpu_clock_mhz", "cpu_clock_mhz", "disk_read_bps", "disk_write_bps", "net_rx_bps", "net_tx_bps"};
    return metric > 0 && metric < POCKET_PC_METRIC_COUNT ? keys[metric] : "";
}
unsigned pocket_pc_pages(const pocket_pc_display_t *display) { return (display->count+POCKET_PC_ROWS-1)/POCKET_PC_ROWS; }
void pocket_pc_navigate(pocket_model_t *model, unsigned *subpage, unsigned pages, int direction) {
    if (!direction) return;
    if (!pages) pages=1;
    if (*subpage>=pages) *subpage=0;
    if (model->page==POCKET_PC) {
        if (direction>0 && *subpage+1<pages) { ++*subpage; return; }
        if (direction<0 && *subpage>0) { --*subpage; return; }
    }
    pocket_model_turn(model,direction);
    *subpage=model->page==POCKET_PC && direction<0 ? pages-1 : 0;
}
void pocket_pc_data_format(const pocket_pc_data_t *data, pocket_pc_metric_t metric, char *text, size_t size) {
    snprintf(text,size,"不可用");
    if ((unsigned)metric >= POCKET_PC_METRIC_COUNT || !metric || !(data->valid & (1u<<metric))) return;
    double v=data->value[metric]; if (!isfinite(v) || v<0) return;
    if (metric==POCKET_PC_CONNECTION) snprintf(text,size,"%s",v ? "已连接" : "数据已过期");
    else if (metric==POCKET_PC_UPTIME && v<=UINT32_MAX) { uint64_t t=(uint64_t)v; snprintf(text,size,"%llu天%llu时",(unsigned long long)(t/86400),(unsigned long long)(t%86400/3600)); }
    else if (metric==POCKET_PC_GPU_TEMP && v<=150) snprintf(text,size,"%.0f °C",v);
    else if ((metric>=POCKET_PC_RAM_USED && metric<=POCKET_PC_VRAM_TOTAL) || metric==POCKET_PC_DISK_FREE) snprintf(text,size,"%.1f GiB",v/1073741824.0);
    else if (metric==POCKET_PC_GPU_POWER && v<=5000) snprintf(text,size,"%.1f W",v);
    else if ((metric==POCKET_PC_GPU_CLOCK || metric==POCKET_PC_CPU_CLOCK) && v<=20000) snprintf(text,size,"%.0f MHz",v);
    else if (metric>=POCKET_PC_DISK_READ) { if(v>=1048576) snprintf(text,size,"%.1f MiB/s",v/1048576.0); else snprintf(text,size,"%.1f KiB/s",v/1024.0); }
    else if (v<=100 && metric!=POCKET_PC_GPU_TEMP && metric!=POCKET_PC_GPU_POWER && metric!=POCKET_PC_GPU_CLOCK && metric!=POCKET_PC_CPU_CLOCK && metric!=POCKET_PC_UPTIME) snprintf(text,size,"%.0f%%",v);
}
