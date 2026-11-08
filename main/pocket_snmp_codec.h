#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define POCKET_OID_ARCS 32
typedef struct { uint32_t arc[POCKET_OID_ARCS]; size_t count; } pocket_oid_t;
typedef struct {
    pocket_oid_t name, oid_value;
    uint8_t type;
    uint64_t number;
} pocket_snmp_value_t;

size_t pocket_snmp_request(uint8_t *output, size_t capacity, const char *community,
                           uint32_t identifier, const pocket_oid_t *oid, bool next);
bool pocket_snmp_response(const uint8_t *bytes, size_t length, const char *community,
                           uint32_t identifier, pocket_snmp_value_t *value);
bool pocket_oid_prefix(const pocket_oid_t *name, const pocket_oid_t *prefix);
int pocket_oid_compare(const pocket_oid_t *left, const pocket_oid_t *right);
