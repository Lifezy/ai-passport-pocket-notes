#include <assert.h>
#include <string.h>
#include "pocket_genshin_diagnostic.h"
int main(void) {
    char output[64];
    pocket_genshin_diagnostic_text(output,sizeof(output),GENSHIN_FP,-1,403,-403);
    assert(!strcmp(output,"设备校验 H403"));
    pocket_genshin_diagnostic_text(output,sizeof(output),GENSHIN_QR,-1,200,10035);
    assert(!strcmp(output,"创建扫码 A10035"));
    pocket_genshin_diagnostic_text(output,sizeof(output),GENSHIN_FP,0x8001,0,0);
    assert(!strcmp(output,"设备校验 E8001"));
    pocket_genshin_diagnostic_text(output,sizeof(output),(pocket_genshin_stage_t)99,0x101,0,0);
    assert(!strcmp(output,"原神请求 E101"));
    char tiny[1]={'x'};
    pocket_genshin_diagnostic_text(tiny,sizeof(tiny),GENSHIN_FP,1,0,0);assert(!tiny[0]);
    return 0;
}
