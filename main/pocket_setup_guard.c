#include "pocket_setup_guard.h"
#include <stdio.h>
#include <string.h>

bool pocket_setup_authorized(const char *expected_key, const char *key,
                             const char *host, const char *address, const char *origin, bool write) {
    if (!expected_key || !key || strlen(expected_key) != 32 || strlen(key) != 32 ||
        !host || !address || !*address) return false;
    unsigned difference = 0;
    for (unsigned i = 0; i < 32; ++i) difference |= (unsigned char)expected_key[i] ^ (unsigned char)key[i];
    char with_port[48], expected_origin[64];
    snprintf(with_port, sizeof(with_port), "%s:80", address);
    if (difference || (strcmp(host, address) && strcmp(host, with_port))) return false;
    snprintf(expected_origin, sizeof(expected_origin), "http://%s", host);
    return !write || (origin && strcmp(origin, expected_origin) == 0);
}

bool pocket_setup_wifi_valid(const char *ssid, const char *password) {
    if (!ssid || !password || strlen(ssid) > 32 || strlen(password) > 64) return false;
    for (const unsigned char *p = (const unsigned char *)ssid; *p; ++p) if (*p < 32) return false;
    for (const unsigned char *p = (const unsigned char *)password; *p; ++p) if (*p < 32) return false;
    size_t length = strlen(password);
    if (length && length < 8) return false;
    if (length == 64)
        for (const char *p = password; *p; ++p)
            if (!((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F'))) return false;
    return true;
}
