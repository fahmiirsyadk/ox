// config.h - Configuration parser
#ifndef CONFIG_H
#define CONFIG_H

// Config entry
typedef struct ConfigEntry {
    char *key;
    char *value;
    struct ConfigEntry *next;
} ConfigEntry;

// Config block
typedef struct ConfigBlock {
    char *name;
    char *type;
    ConfigEntry *entries;
    struct ConfigBlock *children;
    struct ConfigBlock *next;
} ConfigBlock;

// Config structure
typedef struct Config {
    ConfigBlock *bars;
} Config;

// Parse config file
Config *config_parse(const char *path);

// Free config
void config_free(Config *config);

// Get value from block
const char *config_get(ConfigBlock *block, const char *key);

#endif // CONFIG_H
