#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "pocket_pc_display.h"

int main(void) {
    pocket_pc_display_t display;
    char text[POCKET_PC_DISPLAY_BYTES];
    pocket_pc_display_default(&display);
    assert(pocket_pc_display_format(&display, text, sizeof(text)));
    assert(!strcmp(text, "cpu,gpu,gpu_temp,memory,disk,status"));
    assert(!strcmp(pocket_pc_metric_label(display.metric[2]), "GPU 温度"));
    assert(pocket_pc_display_parse("gpu_memory,disk_free,uptime", &display));
    assert(display.count == 3 && display.metric[0] == POCKET_PC_GPU_MEMORY);
    assert(display.metric[3] == POCKET_PC_NONE);
    assert(pocket_pc_display_format(&display, text, sizeof(text)));
    assert(!strcmp(text, "gpu_memory,disk_free,uptime"));
    pocket_pc_display_t many;
    assert(pocket_pc_display_parse("cpu,gpu,gpu_temp,memory,gpu_memory,disk,disk_free",&many));
    assert(many.count==7 && pocket_pc_pages(&many)==2);
    pocket_model_t model;pocket_model_init(&model);model.page=POCKET_PC;unsigned subpage=0;
    pocket_pc_navigate(&model,&subpage,2,1);assert(model.page==POCKET_PC && subpage==1);
    pocket_pc_navigate(&model,&subpage,2,1);assert(model.page==POCKET_GENSHIN && subpage==0);
    pocket_pc_navigate(&model,&subpage,2,-1);assert(model.page==POCKET_PC && subpage==1);
    pocket_pc_navigate(&model,&subpage,2,-1);assert(model.page==POCKET_PC && subpage==0);
    pocket_page_layout_t only;assert(pocket_page_layout_parse("-genshin,-codex,-nas,-home,pc",&only));
    assert(pocket_model_layout(&model,&only));pocket_pc_navigate(&model,&subpage,2,-1);assert(subpage==1 && model.page==POCKET_PC);
    pocket_pc_data_t data={0};data.valid=1u<<POCKET_PC_GPU_POWER;data.value[POCKET_PC_GPU_POWER]=12.5;
    pocket_pc_data_format(&data,POCKET_PC_GPU_POWER,text,sizeof(text));assert(!strcmp(text,"12.5 W"));
    pocket_pc_data_format(&data,POCKET_PC_CPU,text,sizeof(text));assert(!strcmp(text,"不可用"));
    pocket_pc_display_t saved = display;
    const char *bad[] = {"", "cpu,cpu", ",cpu", "cpu,", "cpu,,gpu", " cpu", "CPU", "unknown", "gpu_temp, memory",
        "cpu,gpu,gpu_temp,memory,gpu_memory,disk,disk_free,unknown"};
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        assert(!pocket_pc_display_parse(bad[i], &display));
        assert(!memcmp(&display, &saved, sizeof(display)));
    }
    char long_text[200]; memset(long_text, 'x', sizeof(long_text)); long_text[199] = 0;
    assert(!pocket_pc_display_parse(long_text, &display));
    assert(!pocket_pc_display_parse(NULL, &display));
    assert(!pocket_pc_display_format(&display, text, 1));
    assert(pocket_pc_display_parse("gpu_temp", &display) && display.count == 1);
    assert(!strcmp(pocket_pc_metric_label(POCKET_PC_NONE), ""));
    display.metric[0] = POCKET_PC_METRIC_COUNT;
    assert(!pocket_pc_display_format(&display, text, sizeof(text)));
    puts("PC display selection: PASS");
    return 0;
}
