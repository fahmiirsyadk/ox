// config.c - Configuration parser

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "config.h"

// Skip whitespace
static const char *skip_ws(const char *s) {
    while (isspace((unsigned char)*s)) {
        s++;
    }
    return s;
}

// Read a word (non-whitespace)
static const char *read_word(const char *s, char *buf, size_t len) {
    size_t i = 0;
    while (s[i] != '\0' && !isspace((unsigned char)s[i]) &&
           s[i] != '{' && s[i] != '}') {
        if (i < len - 1) {
            buf[i] = s[i];
        }
        i++;
    }
    if (i < len) {
        buf[i] = '\0';
    }
    return s + i;
}

// Read a quoted string
static const char *read_string(const char *s, char *buf, size_t len) {
    if (*s != '"') {
        return read_word(s, buf, len);
    }
    s++; // skip opening quote
    size_t i = 0;
    while (*s != '\0' && *s != '"') {
        if (i < len - 1) {
            buf[i] = *s;
        }
        i++;
        s++;
    }
    if (*s == '"') {
        s++; // skip closing quote
    }
    if (i < len) {
        buf[i] = '\0';
    }
    return s;
}

// Parse a key-value pair
static const char *parse_entry(const char *s, ConfigEntry **entry) {
    char key[256] = {0};
    char value[256] = {0};

    s = skip_ws(s);
    if (*s == '\0' || *s == '}' || *s == '#') {
        return s;
    }

    s = read_word(s, key, sizeof(key));
    s = skip_ws(s);

    if (*s == '=') {
        s++; // skip =
        s = skip_ws(s);
        s = read_string(s, value, sizeof(value));
    } else {
        // Value is the next word
        s = read_string(s, value, sizeof(value));
    }

    if (key[0] != '\0') {
        ConfigEntry *e = calloc(1, sizeof(ConfigEntry));
        e->key = strdup(key);
        e->value = strdup(value);
        e->next = *entry;
        *entry = e;
    }

    return s;
}

// Parse a block
static const char *parse_block(const char *s, ConfigBlock **block);

static const char *parse_block_content(const char *s, ConfigBlock *block) {
    ConfigEntry **last_entry = &block->entries;
    ConfigBlock **last_child = &block->children;

    while (*s != '\0') {
        s = skip_ws(s);

        if (*s == '\0') {
            break;
        }

        if (*s == '#') {
            while (*s != '\0' && *s != '\n') {
                s++;
            }
            continue;
        }

        if (*s == '}') {
            s++;
            break;
        }

        // Try to parse as a sub-block: "type name { ... }" or "type { ... }"
        char word1[256] = {0};
        char word2[256] = {0};
        const char *tmp = read_word(s, word1, sizeof(word1));
        const char *tmp2 = skip_ws(tmp);

        if (*tmp2 != '\0' && *tmp2 != '}' && *tmp2 != '#') {
            // Read second word
            const char *tmp3 = read_word(tmp2, word2, sizeof(word2));
            const char *tmp4 = skip_ws(tmp3);

            if (*tmp4 == '{') {
                // It's a sub-block with name: "widget time { ... }"
                ConfigBlock *child = NULL;
                s = parse_block(s, &child);
                if (child != NULL) {
                    *last_child = child;
                    last_child = &child->next;
                }
                continue;
            } else if (strcmp(word1, "widget") == 0 && word2[0] != '\0') {
                // "widget name" without braces - create with default settings
                ConfigBlock *child = calloc(1, sizeof(ConfigBlock));
                child->type = strdup("widget");
                child->name = strdup(word2);
                *last_child = child;
                last_child = &child->next;
                s = tmp2;
                continue;
            }
        }

        // Check if just "type { ... }" (no name)
        if (*tmp2 == '{') {
            ConfigBlock *child = NULL;
            s = parse_block(s, &child);
            if (child != NULL) {
                *last_child = child;
                last_child = &child->next;
            }
            continue;
        }

        // It's a key-value entry
        s = parse_entry(s, last_entry);
        if (*last_entry != NULL) {
            last_entry = &(*last_entry)->next;
        }
    }

    return s;
}

static const char *parse_block(const char *s, ConfigBlock **block) {
    s = skip_ws(s);

    char type[256] = {0};
    char name[256] = {0};

    s = read_word(s, type, sizeof(type));
    s = skip_ws(s);
    s = read_word(s, name, sizeof(name));
    s = skip_ws(s);

    if (*s != '{') {
        return s;
    }
    s++; // skip {

    ConfigBlock *b = calloc(1, sizeof(ConfigBlock));
    b->type = strdup(type);
    b->name = strdup(name);

    s = parse_block_content(s, b);

    *block = b;
    return s;
}

Config *config_parse(const char *path) {
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        return NULL;
    }

    // Read file
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *content = calloc(1, size + 1);
    if (content == NULL) {
        fclose(f);
        return NULL;
    }
    fread(content, 1, size, f);
    fclose(f);

    // Parse
    Config *cfg = calloc(1, sizeof(Config));
    ConfigBlock **last_bar = &cfg->bars;

    const char *s = content;
    while (*s != '\0') {
        s = skip_ws(s);
        if (*s == '\0') {
            break;
        }

        if (*s == '#') {
            while (*s != '\0' && *s != '\n') {
                s++;
            }
            continue;
        }

        ConfigBlock *bar = NULL;
        s = parse_block(s, &bar);
        if (bar != NULL) {
            *last_bar = bar;
            last_bar = &bar->next;
        }
    }

    free(content);
    return cfg;
}

void config_free(Config *config) {
    if (config == NULL) {
        return;
    }

    ConfigBlock *bar = config->bars;
    while (bar != NULL) {
        ConfigBlock *next_bar = bar->next;

        // Free entries
        ConfigEntry *entry = bar->entries;
        while (entry != NULL) {
            ConfigEntry *next = entry->next;
            free(entry->key);
            free(entry->value);
            free(entry);
            entry = next;
        }

        // Free children
        ConfigBlock *child = bar->children;
        while (child != NULL) {
            ConfigBlock *next_child = child->next;
            free(child->type);
            free(child->name);
            // Free child entries
            ConfigEntry *ce = child->entries;
            while (ce != NULL) {
                ConfigEntry *next_ce = ce->next;
                free(ce->key);
                free(ce->value);
                free(ce);
                ce = next_ce;
            }
            free(child);
            child = next_child;
        }

        free(bar->type);
        free(bar->name);
        free(bar);
        bar = next_bar;
    }

    free(config);
}

const char *config_get(ConfigBlock *block, const char *key) {
    if (block == NULL || key == NULL) {
        return NULL;
    }

    for (ConfigEntry *entry = block->entries; entry != NULL; entry = entry->next) {
        if (strcmp(entry->key, key) == 0) {
            return entry->value;
        }
    }

    return NULL;
}
