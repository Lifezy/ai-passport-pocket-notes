#include <assert.h>
#include <string.h>
#include "pocket_snmp_codec.h"

int main(void) {
    const pocket_oid_t uptime = {{1,3,6,1,2,1,1,3,0}, 9};
    uint8_t packet[512];
    size_t length = pocket_snmp_request(packet, sizeof(packet), "public", 7, &uptime, false);
    assert(length == 40);
    assert(packet[13] == 0xa0);
    packet[13] = 0xa2;
    ++packet[1]; ++packet[14]; ++packet[25]; ++packet[27];
    packet[length - 2] = 0x43; packet[length - 1] = 1; packet[length++] = 42;
    pocket_snmp_value_t value;
    assert(pocket_snmp_response(packet, length, "public", 7, &value));
    assert(value.type == 0x43 && value.number == 42);
    assert(pocket_oid_compare(&value.name, &uptime) == 0);
    for (size_t cut = 0; cut < length; ++cut) assert(!pocket_snmp_response(packet, cut, "public", 7, &value));
    assert(!pocket_snmp_response(packet, length, "wrong", 7, &value));
    assert(!pocket_snmp_response(packet, length, "public", 8, &value));
    packet[4] = 0;
    assert(!pocket_snmp_response(packet, length, "public", 7, &value));
    for (size_t size = 0; size < 40; ++size) assert(pocket_snmp_request(packet, size, "public", 7, &uptime, false) == 0);
    pocket_oid_t bad = {{1,40}, 2};
    assert(!pocket_snmp_request(packet, sizeof(packet), "public", 7, &bad, false));
    assert(!pocket_snmp_request(packet, sizeof(packet), "public", 0, &uptime, false));
    const pocket_oid_t prefix = {{1,3,6,1,2,1}, 6};
    assert(pocket_oid_prefix(&uptime, &prefix));
    assert(!pocket_oid_prefix(&prefix, &uptime));
    return 0;
}
