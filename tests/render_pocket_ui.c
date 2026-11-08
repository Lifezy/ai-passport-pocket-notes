/* Real LVGL renderer with synthetic fixture data; no board/network access. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "lvgl.h"
#include "pocket_model.h"
#include "pocket_ui.h"
#ifdef NDEBUG
#error "The LVGL acceptance renderer requires enabled assertions"
#endif
LV_FONT_DECLARE(pocket_font_16);

static uint16_t framebuffer[240 * 320], partial[240 * 20];

static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels) {
    const uint16_t *input = (const uint16_t *)pixels;
    int width = area->x2 - area->x1 + 1;
    for (int y = area->y1; y <= area->y2; ++y)
        for (int x = area->x1; x <= area->x2; ++x)
            framebuffer[y * 240 + x] = input[(y - area->y1) * width + x - area->x1];
    lv_display_flush_ready(display);
}

static void save_frame(const char *directory, unsigned index) {
    char path[1024]; snprintf(path, sizeof(path), "%s/page-%u.ppm", directory, index);
    FILE *file = fopen(path, "wb"); assert(file);
    fprintf(file, "P6\n240 320\n255\n");
    for (unsigned y = 0; y < 320; ++y) {
        for (unsigned x = 0; x < 240; ++x) {
            uint16_t value = framebuffer[y * 240 + x];
            unsigned char pixel[] = {(unsigned char)(((value >> 11) & 31) * 255 / 31),
                                      (unsigned char)(((value >> 5) & 63) * 255 / 63),
                                      (unsigned char)((value & 31) * 255 / 31)};
            fwrite(pixel, 1, 3, file);
        }
    }
    fclose(file);
}

int main(int argc, char **argv) {
    setbuf(stdout, NULL);
    assert(argc == 3);
    lv_init();
    lv_display_t *display = lv_display_create(240, 320); assert(display);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(display, flush);
    lv_display_set_buffers(display, partial, NULL, sizeof(partial), LV_DISPLAY_RENDER_MODE_PARTIAL);
    pocket_model_t model;
    pocket_model_init(&model);
    pocket_ui_create();
    assert(pocket_ui_fonts_valid());
    FILE *inventory = fopen(argv[2], "r"); assert(inventory);
    unsigned codepoint, checked = 0;
    while (fscanf(inventory, "%x", &codepoint) == 1) {
        lv_font_glyph_dsc_t glyph = {0};
        assert(lv_font_get_glyph_dsc(&pocket_font_16, &glyph, codepoint, 0) && !glyph.is_placeholder);
        ++checked;
    }
    fclose(inventory);
    lv_font_glyph_dsc_t missing = {0};
    assert(!lv_font_get_glyph_dsc(&pocket_font_16, &missing, 0x1f600, 0) || missing.is_placeholder);
    printf("Runtime font coverage: PASS (%u characters)\n", checked);
    const char *const examples[POCKET_PAGE_COUNT][POCKET_ROW_COUNT] = {
        {"192/200", "1时04分", "3/4", "未领取", "4/5", "1860/2400"},
        {"75%", "11/07 19:00", "42%", "11/10 08:00", "12.50", "2 / 11/10"},
        {"18%", "63%", "58%", "1274.2 GiB", "12天6时", "已连接"},
        {"客厅主灯", "已开启", "亮度降低", "-20.0", "尚未执行", "长按确定返回"},
        {"27%", "50%", "62 °C", "41%", "62%", "已连接"},
    };
    for (unsigned page = 0; page < POCKET_PAGE_COUNT; ++page) {
        model.page = (pocket_page_t)page;
        model.snapshot[page].has_data = true;
        model.snapshot[page].status = POCKET_READY;
        model.snapshot[page].updated_at = 1793980800;
        strcpy(model.snapshot[page].message, "布局测试，非实时数据");
        for (unsigned row = 0; row < 6; ++row) strcpy(model.snapshot[page].value[row], examples[page][row]);
        pocket_ui_render(&model, 83);
        lv_refr_now(display);
        save_frame(argv[1], page);
    }
    /* Alternate selection and hidden rows must not leak into other pages. */
    pocket_pc_display_t custom;
    assert(pocket_pc_display_parse("gpu_memory,disk_free,uptime", &custom));
    assert(custom.count == 3 && custom.metric[0] == POCKET_PC_GPU_MEMORY);
    pocket_ui_pc_display(&custom);
    model.page = POCKET_PC;
    strcpy(model.snapshot[POCKET_PC].value[0], "68%");
    strcpy(model.snapshot[POCKET_PC].value[1], "185.6 GiB");
    strcpy(model.snapshot[POCKET_PC].value[2], "2天3时");
    pocket_ui_render(&model, 83); lv_refr_now(display); save_frame(argv[1], 7);
    model.page = POCKET_NAS; pocket_ui_render(&model, 83); lv_refr_now(display);
    pocket_pc_display_default(&custom); pocket_ui_pc_display(&custom);
    pocket_page_layout_t navigation;
    assert(pocket_page_layout_parse("nas,pc,-genshin,-codex,-home", &navigation));
    assert(pocket_model_layout(&model, &navigation) && model.page == POCKET_NAS);
    pocket_ui_render(&model, 83); lv_refr_now(display); save_frame(argv[1], 8);
    pocket_model_turn(&model, 1); assert(model.page == POCKET_PC);
    pocket_model_turn(&model, 1); assert(model.page == POCKET_NAS);
    pocket_page_layout_default(&navigation); assert(pocket_model_layout(&model, &navigation));
    assert(pocket_pc_display_parse("cpu,gpu,gpu_temp,memory,gpu_memory,disk,disk_free,uptime,status,memory_used_bytes,memory_free_bytes,memory_total_bytes,gpu_memory_used_bytes,gpu_memory_free_bytes,gpu_memory_total_bytes,gpu_power_w,gpu_fan_percent,gpu_clock_mhz,cpu_clock_mhz,disk_read_bps,disk_write_bps,net_rx_bps,net_tx_bps",&custom));
    assert(custom.count==23 && pocket_pc_pages(&custom)==4);
    pocket_ui_pc_display(&custom);model.page=POCKET_PC;
    pocket_pc_data_t data={0};
    for(unsigned i=1;i<POCKET_PC_METRIC_COUNT;++i) { data.valid|=1u<<i;data.value[i]=42; }
    data.value[POCKET_PC_GPU_TEMP]=62;data.value[POCKET_PC_GPU_POWER]=123.4;
    data.value[POCKET_PC_CPU_CLOCK]=2496;data.value[POCKET_PC_GPU_CLOCK]=1845;
    for(unsigned i=POCKET_PC_RAM_USED;i<=POCKET_PC_VRAM_TOTAL;++i)data.value[i]=4.5*1073741824.0;
    for(unsigned i=POCKET_PC_DISK_READ;i<POCKET_PC_METRIC_COUNT;++i)data.value[i]=12.3*1048576;
    for(unsigned page=0;page<4;++page) {
        pocket_ui_pc_page(page);
        for(unsigned row=0;row<POCKET_ROW_COUNT;++row) {
            unsigned index=page*POCKET_PC_ROWS+row;
            if(index<custom.count)pocket_pc_data_format(&data,custom.metric[index],model.snapshot[POCKET_PC].value[row],POCKET_VALUE_BYTES);
            else model.snapshot[POCKET_PC].value[row][0]=0;
        }
        pocket_ui_render(&model,83);lv_refr_now(display);save_frame(argv[1],9+page);
    }
    pocket_pc_display_default(&custom);pocket_ui_pc_display(&custom);pocket_ui_pc_page(0);
    pocket_display_menu_t menu = {.open=true, .brightness=75, .timeout=60};
    pocket_ui_display_settings(&menu, 83); lv_refr_now(display); save_frame(argv[1], 13);
    menu.editing = true; menu.brightness = 10;
    pocket_ui_display_settings(&menu, 83); lv_refr_now(display); save_frame(argv[1], 14);
    menu.row = 1; menu.timeout = 0;
    pocket_ui_display_settings(&menu, 83); lv_refr_now(display); save_frame(argv[1], 15);
    menu.saving = true; pocket_ui_display_settings(&menu, 83); lv_refr_now(display);
    menu.saving = false; menu.error = true;
    pocket_ui_display_settings(&menu, 83); lv_refr_now(display); save_frame(argv[1], 16);
    model.page=POCKET_GENSHIN;
    pocket_setup_view_t setup = {.hotspot = true, .wifi_qr = true};
    strcpy(setup.ssid, "Passport-AB12"); strcpy(setup.password, "SAMPLE23456");
    strcpy(setup.address, "192.168.4.1");
    strcpy(setup.url, "http://192.168.4.1/?k=0123456789abcdef0123456789abcdef");
    strcpy(setup.qr, "WIFI:T:WPA;S:Passport-AB12;P:SAMPLE23456;;");
    pocket_ui_setup(&setup, 83); lv_refr_now(display); save_frame(argv[1], POCKET_PAGE_COUNT);
    setup.wifi_qr = false; strcpy(setup.qr, setup.url);
    pocket_ui_setup(&setup, 83); lv_refr_now(display); save_frame(argv[1], POCKET_PAGE_COUNT + 1);
    pocket_ui_render(&model, 83);
    pocket_ui_qr("");
    lv_refr_now(display);
    for (unsigned iteration = 0; iteration < 100; ++iteration) {
        char qr[1001];
        memset(qr, 'a', sizeof(qr) - 1); qr[1000] = '\0';
        memcpy(qr, "https://example.invalid/", 24);
        pocket_ui_qr(iteration % 2 ? qr : "https://example.invalid/pocket-notes-layout-test");
        lv_refr_now(display);
        pocket_ui_qr("");
        lv_refr_now(display);
    }
    lv_mem_monitor_t memory;
    lv_mem_monitor(&memory);
    assert(memory.free_size > 2048);
    printf("LVGL render: PASS; pool free %zu bytes\n", memory.free_size);
    lv_deinit();
    return 0;
}
