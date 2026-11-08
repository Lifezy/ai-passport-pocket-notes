#include "pocket_ui.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "lvgl.h"

LV_FONT_DECLARE(pocket_font_16);

static const char *const TITLES[] = {"原神便笺", "Codex 额度", "NAS 状态", "米家遥控", "电脑监控"};
static const char *const STATUS[] = {
    "等待设置", "等待网络", "正在更新", "数据已更新", "数据已过期", "需要登录", "连接失败",
};
static const char *const LABELS[POCKET_PAGE_COUNT][POCKET_ROW_COUNT] = {
    {"树脂", "恢复剩余", "每日委托", "委托奖励", "探索派遣", "洞天宝钱"},
    {"本期剩余", "重置时间", "次期剩余", "重置时间", "额度余额", "重置卡/到期"},
    {"CPU 占用", "内存占用", "存储占用", "可用空间", "运行时间", "连接状态"},
    {"设备", "设备状态", "控制项", "参数/序号", "操作结果", "操作提示"},
    {"CPU 占用", "GPU 占用", "GPU 温度", "内存占用", "系统盘占用", "连接状态"},
};
static const uint32_t ACCENTS[] = {0x52C9A3, 0xA2AFFA, 0xF4BD73, 0xF29DA9, 0x76BCEB};
static lv_obj_t *s_screen, *s_title, *s_battery, *s_status, *s_updated;
static lv_obj_t *s_rows[POCKET_ROW_COUNT], *s_names[POCKET_ROW_COUNT], *s_values[POCKET_ROW_COUNT];
static lv_obj_t *s_dots[POCKET_PAGE_COUNT];
static bool s_fonts_valid;
static pocket_pc_display_t s_pc_display;
static unsigned s_pc_page;
void pocket_ui_pc_page(unsigned page) { s_pc_page=page; }
static lv_obj_t *s_hint, *s_qr_panel, *s_qr;
static char s_qr_url[1024];

static bool has_text(const char *text) {
    while (*text) {
        uint32_t codepoint = (unsigned char)*text++;
        unsigned continuation = 0;
        if (codepoint >= 0xf0) { codepoint &= 7; continuation = 3; }
        else if (codepoint >= 0xe0) { codepoint &= 15; continuation = 2; }
        else if (codepoint >= 0xc0) { codepoint &= 31; continuation = 1; }
        else if (codepoint >= 0x80) return false;
        for (unsigned i = 0; i < continuation; ++i) {
            unsigned char next = (unsigned char)*text;
            if ((next & 0xc0) != 0x80) return false;
            codepoint = (codepoint << 6) | (next & 63);
            ++text;
        }
        if (codepoint == '\n') continue;
        lv_font_glyph_dsc_t glyph = {0};
        if (!lv_font_get_glyph_dsc(&pocket_font_16, &glyph, codepoint, 0) || glyph.is_placeholder) return false;
    }
    return true;
}

static lv_obj_t *label(lv_obj_t *parent, int x, int y, int width, uint32_t color) {
    lv_obj_t *object = lv_label_create(parent);
    lv_obj_set_pos(object, x, y);
    lv_obj_set_width(object, width);
    lv_label_set_long_mode(object, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(object, &pocket_font_16, 0);
    lv_obj_set_style_text_color(object, lv_color_hex(color), 0);
    return object;
}

void pocket_ui_pc_display(const pocket_pc_display_t *display) {
    s_pc_display = *display;
}

void pocket_ui_create(void) {
    if (!s_pc_display.count) pocket_pc_display_default(&s_pc_display);
    s_fonts_valid = true;
    for (unsigned page = 0; page < POCKET_PAGE_COUNT; ++page) {
        s_fonts_valid &= has_text(TITLES[page]);
        for (unsigned row = 0; row < POCKET_ROW_COUNT; ++row) s_fonts_valid &= has_text(LABELS[page][row]);
    }
    for (unsigned i = 0; i < sizeof(STATUS) / sizeof(STATUS[0]); ++i) s_fonts_valid &= has_text(STATUS[i]);
    for (unsigned i = 1; i < POCKET_PC_METRIC_COUNT; ++i)
        s_fonts_valid &= has_text(pocket_pc_metric_label((pocket_pc_metric_t)i));
    s_fonts_valid &= has_text("上次 未同步 中文字形不支持 上下翻页 确定刷新");
    s_fonts_valid &= has_text("屏幕设置 屏幕亮度 自动息屏 不自动息屏 正在保存 保存失败，请重试 上下调整 确定保存 上下选择 确定调整 返回 秒 亮度实时预览 保存后重启仍保留 长按确定返回 双击确定 长按取消");

    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0x101B2B), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    s_title = label(s_screen, 23, 17, 156, 0xF5F4EF);
    s_battery = label(s_screen, 179, 18, 43, 0xC3CEDE);
    lv_obj_set_style_text_align(s_battery, LV_TEXT_ALIGN_RIGHT, 0);
    s_status = label(s_screen, 23, 43, 195, 0xA5B5C9);
    for (unsigned i = 0; i < POCKET_ROW_COUNT; ++i) {
        lv_obj_t *row = lv_obj_create(s_screen);
        s_rows[i] = row;
        lv_obj_set_pos(row, 15, 71 + (int)i * 31);
        lv_obj_set_size(row, 210, 29);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(row, 7, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x1C2A3E), 0);
        s_names[i] = label(row, 9, 5, 95, 0xB7C6D8);
        s_values[i] = label(row, 104, 5, 97, 0xF5F4EF);
        lv_obj_set_style_text_align(s_values[i], LV_TEXT_ALIGN_RIGHT, 0);
    }
    s_updated = label(s_screen, 23, 262, 194, 0x91A4BB);
    for (unsigned i = 0; i < POCKET_PAGE_COUNT; ++i) {
        s_dots[i] = lv_obj_create(s_screen);
        lv_obj_set_pos(s_dots[i], (240 - (POCKET_PAGE_COUNT * 14 - 8)) / 2 + (int)i * 14, 286);
        lv_obj_set_size(s_dots[i], 6, 6);
        lv_obj_set_style_radius(s_dots[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(s_dots[i], 0, 0);
    }
    s_hint = label(s_screen, 27, 299, 186, 0x91A4BB);
    lv_obj_set_style_text_align(s_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_hint, "上下翻页  确定刷新");
    lv_screen_load(s_screen);
}

void pocket_ui_hint(const char *text) { lv_label_set_text(s_hint, text); }

void pocket_ui_qr(const char *url) {
    if (!url || !*url) {
        if (s_qr_panel) lv_obj_delete(s_qr_panel);
        s_qr_panel = NULL; s_qr = NULL; s_qr_url[0] = '\0';
        return;
    }
    if (!s_qr_panel) {
        s_qr_panel = lv_obj_create(s_screen);
        lv_obj_set_pos(s_qr_panel, 12, 66); lv_obj_set_size(s_qr_panel, 216, 214);
        lv_obj_set_style_pad_all(s_qr_panel, 0, 0);
        lv_obj_set_style_border_width(s_qr_panel, 0, 0);
        lv_obj_set_style_bg_color(s_qr_panel, lv_color_white(), 0);
        lv_obj_remove_flag(s_qr_panel, LV_OBJ_FLAG_SCROLLABLE);
        s_qr = lv_qrcode_create(s_qr_panel);
        lv_qrcode_set_size(s_qr, 184);
        lv_qrcode_set_dark_color(s_qr, lv_color_black());
        lv_qrcode_set_light_color(s_qr, lv_color_white());
        lv_obj_center(s_qr);
    }
    if (strcmp(s_qr_url, url) != 0) {
        if (lv_qrcode_update(s_qr, url, strlen(url)) == LV_RESULT_OK)
            snprintf(s_qr_url, sizeof(s_qr_url), "%s", url);
    }
}

bool pocket_ui_fonts_valid(void) { return s_fonts_valid; }

void pocket_ui_render(const pocket_model_t *model, int battery_percent) {
    lv_obj_set_y(s_updated, 262); lv_obj_set_y(s_hint, 299);
    for (unsigned i = 0; i < POCKET_PAGE_COUNT; ++i) lv_obj_remove_flag(s_dots[i], LV_OBJ_FLAG_HIDDEN);
    lv_label_set_long_mode(s_updated, LV_LABEL_LONG_DOT);
    unsigned page = (unsigned)model->page;
    const pocket_snapshot_t *snapshot = &model->snapshot[page];
    lv_color_t accent = lv_color_hex(ACCENTS[page]);
    if(page==POCKET_PC && pocket_pc_pages(&s_pc_display)>1) lv_label_set_text_fmt(s_title,"电脑监控 %u/%u",s_pc_page+1,pocket_pc_pages(&s_pc_display));
    else lv_label_set_text(s_title, TITLES[page]);
    lv_obj_set_style_text_color(s_title, accent, 0);
    if (battery_percent >= 0 && battery_percent <= 100) lv_label_set_text_fmt(s_battery, "%d%%", battery_percent);
    else lv_label_set_text(s_battery, "--%");
    const char *status = snapshot->message[0] ? snapshot->message : STATUS[snapshot->status];
    lv_label_set_text(s_status, has_text(status) ? status : "中文字形不支持");
    for (unsigned row = 0; row < POCKET_ROW_COUNT; ++row) {
        lv_obj_set_style_bg_color(s_rows[row], lv_color_hex(0x1C2A3E), 0);
        if (page == POCKET_PC && row + s_pc_page*POCKET_PC_ROWS >= s_pc_display.count) lv_obj_add_flag(s_rows[row], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_remove_flag(s_rows[row], LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_names[row], page == POCKET_PC ? pocket_pc_metric_label(row+s_pc_page*POCKET_PC_ROWS < s_pc_display.count ? s_pc_display.metric[row+s_pc_page*POCKET_PC_ROWS] : POCKET_PC_NONE) : LABELS[page][row]);
        const char *value = snapshot->value[row];
        lv_label_set_long_mode(s_values[row], page == POCKET_MIHOME && row == 0 ? LV_LABEL_LONG_SCROLL_CIRCULAR : LV_LABEL_LONG_DOT);
        lv_label_set_text(s_values[row], has_text(value) ? value : "字形不支持");
    }
    char updated[48] = "未同步";
    if (snapshot->has_data && snapshot->updated_at <= 0) snprintf(updated, sizeof(updated), "已更新，等待校时");
    if (snapshot->has_data && snapshot->updated_at > 0) {
        time_t seconds = (time_t)snapshot->updated_at;
        struct tm local;
#ifdef _WIN32
        localtime_s(&local, &seconds);
#else
        localtime_r(&seconds, &local);
#endif
        char clock[24];
        strftime(clock, sizeof(clock), "%m/%d %H:%M", &local);
        snprintf(updated, sizeof(updated), "上次 %s", clock);
    }
    lv_label_set_text(s_updated, updated);
    unsigned visible = 0;
    for (unsigned i = 0; i < POCKET_PAGE_COUNT; ++i) visible += model->layout.enabled[i];
    unsigned dot = 0;
    for (unsigned i = 0; i < POCKET_PAGE_COUNT; ++i) {
        pocket_page_t id = model->layout.order[i];
        if (!model->layout.enabled[id]) continue;
        lv_obj_set_x(s_dots[dot], (240 - (visible * 14 - 8)) / 2 + (int)dot * 14);
        lv_obj_set_style_bg_color(s_dots[dot], id == model->page ? accent : lv_color_hex(0x42536B), 0);
        lv_obj_set_style_bg_opa(s_dots[dot++], LV_OPA_COVER, 0);
    }
    for (; dot < POCKET_PAGE_COUNT; ++dot) lv_obj_add_flag(s_dots[dot], LV_OBJ_FLAG_HIDDEN);
}

void pocket_ui_display_settings(const pocket_display_menu_t *menu, int battery_percent) {
    pocket_ui_qr("");
    lv_label_set_text(s_title, "屏幕设置");
    lv_obj_set_style_text_color(s_title, lv_color_hex(0x52C9A3), 0);
    if (battery_percent >= 0 && battery_percent <= 100) lv_label_set_text_fmt(s_battery, "%d%%", battery_percent);
    else lv_label_set_text(s_battery, "--%");
    lv_label_set_text(s_status, menu->saving ? "正在保存" : menu->error ? "保存失败，请重试" :
        menu->editing ? "上下调整  确定保存" : "上下选择  确定调整");
    const char *names[] = {"屏幕亮度", "自动息屏", "返回"};
    for (unsigned row = 0; row < POCKET_ROW_COUNT; ++row) {
        if (row >= 3) { lv_obj_add_flag(s_rows[row], LV_OBJ_FLAG_HIDDEN); continue; }
        lv_obj_remove_flag(s_rows[row], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(s_rows[row], lv_color_hex(row == menu->row ? 0x264B53 : 0x1C2A3E), 0);
        lv_label_set_text(s_names[row], names[row]);
        lv_label_set_long_mode(s_values[row], LV_LABEL_LONG_DOT);
        if (!row) lv_label_set_text_fmt(s_values[row], "%u%%", menu->brightness);
        else if (row == 1 && menu->timeout) lv_label_set_text_fmt(s_values[row], "%u 秒", menu->timeout);
        else lv_label_set_text(s_values[row], row == 1 ? "不自动息屏" : "确定");
    }
    for (unsigned i = 0; i < POCKET_PAGE_COUNT; ++i) lv_obj_add_flag(s_dots[i], LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_y(s_updated, 262); lv_label_set_long_mode(s_updated, LV_LABEL_LONG_DOT);
    lv_label_set_text(s_updated, menu->editing ? (menu->row == 0 ? "亮度实时预览" : "确定保存  长按取消") : "保存后重启仍保留");
    lv_obj_set_y(s_hint, 299); lv_label_set_text(s_hint, "长按确定返回");
}

void pocket_ui_setup(const pocket_setup_view_t *view, int battery_percent) {
    lv_obj_set_y(s_updated, 281); lv_obj_set_y(s_hint, 302);
    for (unsigned i = 0; i < POCKET_PAGE_COUNT; ++i) lv_obj_add_flag(s_dots[i], LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s_title, "手机设置");
    lv_obj_set_style_text_color(s_title, lv_color_hex(0x52C9A3), 0);
    if (battery_percent >= 0 && battery_percent <= 100) lv_label_set_text_fmt(s_battery, "%d%%", battery_percent);
    else lv_label_set_text(s_battery, "--%");
    lv_label_set_text(s_status, view->wifi_qr ? "先扫码连接设备热点" : "扫码打开统一设置页");
    for (unsigned row = 0; row < POCKET_ROW_COUNT; ++row) {
        lv_label_set_text(s_names[row], ""); lv_label_set_text(s_values[row], "");
    }
    char description[64];
    if (view->hotspot && view->wifi_qr) snprintf(description, sizeof(description), "热点密码 %s", view->password);
    else snprintf(description, sizeof(description), "%s · 十分钟有效", view->address);
    lv_label_set_text(s_updated, description);
    lv_label_set_long_mode(s_updated, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(s_hint, view->hotspot ? "确定换码  长按确定退出" : "长按确定退出设置");
    pocket_ui_qr(view->qr);
}
