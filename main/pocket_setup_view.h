#pragma once
#include <stdbool.h>
typedef struct {
    bool hotspot, wifi_qr;
    unsigned revision;
    char ssid[24], password[16], address[32], url[128], qr[160];
} pocket_setup_view_t;
