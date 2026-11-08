#pragma once
#include <stdbool.h>
#include <stddef.h>

/* Bound parser recursion for the two device inputs that accept flat objects. */
static inline bool pocket_json_flat_object(const char *text, size_t length) {
    bool quoted = false, escaped = false, opened = false, closed = false;
    unsigned depth = 0;
    for (size_t i = 0; i < length; ++i) {
        unsigned char byte = (unsigned char)text[i];
        if (quoted) {
            if (escaped) escaped = false;
            else if (byte == '\\') escaped = true;
            else if (byte == '"') quoted = false;
            continue;
        }
        bool whitespace = byte == ' ' || byte == '\t' || byte == '\r' || byte == '\n';
        if ((closed || !opened) && whitespace) continue;
        if (closed) return false;
        if (!opened && byte != '{') return false;
        if (byte == '"') quoted = true;
        else if (byte == '[' || byte == ']') return false;
        else if (byte == '{') { if (++depth > 1) return false; opened = true; }
        else if (byte == '}') { if (!depth) return false; --depth; closed = true; }
    }
    return opened && closed && !quoted && depth == 0;
}
