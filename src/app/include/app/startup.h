#pragma once

#include <stdbool.h>
#include <stdint.h>

enum {
    APP_STARTUP_MAGIC = 0x50555453
};

typedef struct ThemeLoader {
    void (*start)(int builtin);
    void (*follow)(int builtin);
    bool (*tend)(void);
    int (*scan)(void);
    bool (*choose)(int index);
    void (*choose_builtin)(int builtin);
    bool (*wear)(const char *name);
} ThemeLoader;

typedef struct AppStartup {
    uint32_t magic;
    void (*start)(void);
    const ThemeLoader *themes;
} AppStartup;

extern const AppStartup APP_STARTUP;
extern const ThemeLoader THEME_LOADER;

const AppStartup *app_startup_open(void);
void app_startup_close(void);
