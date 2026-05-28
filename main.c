// oxbar - A simple, extensible status bar for X11

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>

#include "bar.h"
#include "widget.h"
#include "config.h"

static volatile int running = 1;

static void sig_handler(int sig) {
    (void)sig;
    running = 0;
}

int main(int argc, char *argv[]) {
    const char *config_path = getenv("OXBAR_CONFIG");
    if (config_path == NULL) {
        config_path = getenv("HOME");
        char path[256];
        snprintf(path, sizeof(path), "%s/.config/oxbar/config", config_path);
        config_path = strdup(path);
    }

    if (argc > 1) {
        config_path = argv[1];
    }

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    Config *cfg = config_parse(config_path);
    if (cfg == NULL) {
        fprintf(stderr, "oxbar: failed to parse config\n");
        return 1;
    }

    Display *dpy = XOpenDisplay(NULL);
    if (dpy == NULL) {
        fprintf(stderr, "oxbar: cannot open display\n");
        config_free(cfg);
        return 1;
    }

    int screen = DefaultScreen(dpy);

    // Create bars from config
    Bar *bars = NULL;
    Bar *last_bar = NULL;

    for (ConfigBlock *bar_cfg = cfg->bars; bar_cfg != NULL; bar_cfg = bar_cfg->next) {
        Bar *bar = bar_create(dpy, screen, bar_cfg);
        if (bar != NULL) {
            if (bars == NULL) {
                bars = bar;
            } else {
                last_bar->next = bar;
            }
            last_bar = bar;
        }
    }

    if (bars == NULL) {
        fprintf(stderr, "oxbar: no bars configured\n");
        XCloseDisplay(dpy);
        config_free(cfg);
        return 1;
    }

    // Main loop
    struct timespec ts = { .tv_sec = 0, .tv_nsec = 100000000 }; // 100ms
    struct timespec tick;
    clock_gettime(CLOCK_MONOTONIC, &tick);

    while (running) {
        // Update widgets based on their intervals
        double now = tick.tv_sec + tick.tv_nsec / 1e9;
        for (Bar *bar = bars; bar != NULL; bar = bar->next) {
            for (int i = 0; i < bar->widget_count; i++) {
                Widget *w = bar->widgets[i];
                if (w->interval > 0 && now - w->last_update >= w->interval) {
                    widget_update(w);
                    w->last_update = now;
                }
            }
        }

        for (Bar *bar = bars; bar != NULL; bar = bar->next) {
            bar_render(bar);
        }

        // Process events
        while (XPending(dpy) > 0) {
            XEvent ev;
            XNextEvent(dpy, &ev);
            if (ev.type == ButtonPress) {
                for (Bar *bar = bars; bar != NULL; bar = bar->next) {
                    if (ev.xbutton.window == bar->win) {
                        bar_click(bar, ev.xbutton.x);
                    }
                }
            }
        }

        XFlush(dpy);
        nanosleep(&ts, NULL);
        clock_gettime(CLOCK_MONOTONIC, &tick);
    }

    // Cleanup
    for (Bar *bar = bars; bar != NULL;) {
        Bar *next = bar->next;
        bar_destroy(bar);
        bar = next;
    }

    XCloseDisplay(dpy);
    config_free(cfg);

    return 0;
}
