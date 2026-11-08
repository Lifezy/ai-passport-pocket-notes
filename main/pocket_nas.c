#include "pocket_services.h"
#include "pocket_snmp_codec.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "esp_random.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"

static esp_err_t query(int socket_fd, const char *community, const pocket_oid_t *oid,
                       bool next, pocket_snmp_value_t *value) {
    uint8_t packet[512], response[1500];
    uint32_t identifier = (esp_random() & 0x7fffffff) | 1;
    size_t length = pocket_snmp_request(packet, sizeof(packet), community, identifier, oid, next);
    if (!length) return ESP_ERR_INVALID_ARG;
    if (send(socket_fd, packet, length, 0) != (int)length) return ESP_FAIL;
    int count = recv(socket_fd, response, sizeof(response), 0);
    if (count < 0) return ESP_ERR_TIMEOUT;
    return pocket_snmp_response(response, (size_t)count, community, identifier, value) ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

static bool numeric(const pocket_snmp_value_t *value) {
    return value->type == 2 || (value->type >= 0x41 && value->type <= 0x43) || value->type == 0x46;
}

esp_err_t pocket_nas_refresh(const pocket_config_t *config, pocket_snapshot_t *snapshot) {
    if (!config->nas_community[0]) return ESP_ERR_INVALID_STATE;
    struct sockaddr_in target = {.sin_family = AF_INET, .sin_port = htons(161)};
    if (inet_pton(AF_INET, config->nas_host, &target.sin_addr) != 1) return ESP_ERR_INVALID_ARG;
    int socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_fd < 0) return ESP_FAIL;
    struct timeval timeout = {.tv_sec = 1, .tv_usec = 500000};
    setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    if (connect(socket_fd, (struct sockaddr *)&target, sizeof(target)) != 0) { close(socket_fd); return ESP_FAIL; }
    for (unsigned row = 0; row < POCKET_ROW_COUNT; ++row) snprintf(snapshot->value[row], POCKET_VALUE_BYTES, "--");
    const pocket_oid_t uptime = {{1,3,6,1,2,1,25,1,1,0}, 10};
    const pocket_oid_t cpu_prefix = {{1,3,6,1,2,1,25,3,3,1,2}, 11};
    const pocket_oid_t storage_prefix = {{1,3,6,1,2,1,25,2,3,1,5}, 11};
    const pocket_oid_t memory_type = {{1,3,6,1,2,1,25,2,1,2}, 10};
    const pocket_oid_t disk_type = {{1,3,6,1,2,1,25,2,1,4}, 10};
    pocket_snmp_value_t value;
    esp_err_t error = query(socket_fd, config->nas_community, &uptime, false, &value);
    if (error != ESP_OK) goto finish;
    if (value.type == 0x43 && pocket_oid_compare(&value.name, &uptime) == 0) {
        uint64_t seconds = value.number / 100;
        snprintf(snapshot->value[4], POCKET_VALUE_BYTES, "%llu天%llu时", (unsigned long long)(seconds / 86400), (unsigned long long)(seconds % 86400 / 3600));
    }
    pocket_oid_t cursor = cpu_prefix;
    unsigned cores = 0, cpu_total = 0;
    for (unsigned i = 0; i < 32; ++i) {
        error = query(socket_fd, config->nas_community, &cursor, true, &value);
        if (error != ESP_OK) goto finish;
        if (!pocket_oid_prefix(&value.name, &cpu_prefix) || value.type == 0x82) break;
        if (pocket_oid_compare(&value.name, &cursor) <= 0) { error = ESP_ERR_INVALID_RESPONSE; goto finish; }
        cursor = value.name;
        if (numeric(&value) && value.number <= 100) { cpu_total += (unsigned)value.number; ++cores; }
    }
    if (cores) snprintf(snapshot->value[0], POCKET_VALUE_BYTES, "%u%%", (cpu_total + cores / 2) / cores);
    cursor = storage_prefix;
    uint64_t largest_disk = 0, disk_used = 0;
    bool memory_found = false;
    for (unsigned i = 0; i < 32; ++i) {
        error = query(socket_fd, config->nas_community, &cursor, true, &value);
        if (error != ESP_OK) goto finish;
        if (!pocket_oid_prefix(&value.name, &storage_prefix) || value.type == 0x82) break;
        if (pocket_oid_compare(&value.name, &cursor) <= 0) { error = ESP_ERR_INVALID_RESPONSE; goto finish; }
        cursor = value.name;
        if (!numeric(&value) || cursor.count != 12 || !value.number || value.number > UINT32_MAX) continue;
        uint64_t units_count = value.number;
        pocket_oid_t field = cursor; field.arc[10] = 2;
        error = query(socket_fd, config->nas_community, &field, false, &value);
        if (error != ESP_OK) goto finish;
        if (value.type != 6 || pocket_oid_compare(&value.name, &field) != 0) continue;
        bool memory = pocket_oid_compare(&value.oid_value, &memory_type) == 0;
        bool disk = pocket_oid_compare(&value.oid_value, &disk_type) == 0;
        if (!memory && !disk) continue;
        field.arc[10] = 4;
        error = query(socket_fd, config->nas_community, &field, false, &value);
        if (error != ESP_OK) goto finish;
        if (!numeric(&value) || pocket_oid_compare(&value.name, &field) != 0 || !value.number || value.number > UINT32_MAX) continue;
        uint64_t bytes_per_unit = value.number;
        field.arc[10] = 6;
        error = query(socket_fd, config->nas_community, &field, false, &value);
        if (error != ESP_OK) goto finish;
        if (!numeric(&value) || pocket_oid_compare(&value.name, &field) != 0 || value.number > units_count) continue;
        uint64_t used = value.number;
        if (memory && !memory_found) {
            snprintf(snapshot->value[1], POCKET_VALUE_BYTES, "%llu%%", (unsigned long long)(used * 100 / units_count));
            memory_found = true;
        }
        if (disk && units_count * bytes_per_unit > largest_disk) {
            largest_disk = units_count * bytes_per_unit;
            disk_used = used * bytes_per_unit;
        }
    }
    if (largest_disk) {
        snprintf(snapshot->value[2], POCKET_VALUE_BYTES, "%.0f%%", (double)disk_used * 100.0 / (double)largest_disk);
        snprintf(snapshot->value[3], POCKET_VALUE_BYTES, "%.1f GiB", (double)(largest_disk - disk_used) / 1073741824.0);
    }
    snprintf(snapshot->value[5], POCKET_VALUE_BYTES, "已连接");
    if (!cores && !memory_found && !largest_disk) error = ESP_ERR_NOT_SUPPORTED;
finish:
    close(socket_fd);
    return error;
}
