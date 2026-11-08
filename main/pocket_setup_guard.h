#pragma once
#include <stdbool.h>
bool pocket_setup_authorized(const char *expected_key, const char *key,
                             const char *host, const char *address, const char *origin, bool write);
bool pocket_setup_wifi_valid(const char *ssid, const char *password);
