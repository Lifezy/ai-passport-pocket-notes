#pragma once
#include <stddef.h>
#include <stdio.h>
typedef enum { GENSHIN_FP, GENSHIN_QR, GENSHIN_POLL, GENSHIN_COOKIE, GENSHIN_ROLE, GENSHIN_NOTE, GENSHIN_VERIFY, GENSHIN_STAGE_COUNT } pocket_genshin_stage_t;
static inline void pocket_genshin_diagnostic_text(char *output, size_t capacity,
        pocket_genshin_stage_t stage, int transport, int http, int api) {
    static const char *const labels[] = {"设备校验", "创建扫码", "等待确认", "账号凭据", "查询角色", "读取便笺", "安全验证"};
    const char *label = (unsigned)stage < GENSHIN_STAGE_COUNT ? labels[stage] : "原神请求";
    if (http && (http < 200 || http >= 300)) snprintf(output, capacity, "%s H%d", label, http);
    else if (api) snprintf(output, capacity, "%s A%d", label, api);
    else snprintf(output, capacity, "%s E%X", label, (unsigned)transport);
}
