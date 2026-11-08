#pragma once
/* Narrow declarations; the preference test rejects calls into JSON-update paths. */
typedef struct cJSON {
    struct cJSON *next, *prev, *child;
    int type;
    char *valuestring;
    int valueint;
    double valuedouble;
    char *string;
} cJSON;
cJSON *cJSON_Parse(const char *text);
void cJSON_Delete(cJSON *item);
int cJSON_IsObject(const cJSON *item);
int cJSON_IsString(const cJSON *item);
#define cJSON_ArrayForEach(element, array) for (element = (array) ? (array)->child : 0; element; element = element->next)
