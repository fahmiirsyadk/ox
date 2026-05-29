#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include "ox.h"

struct OxWidget {
    char *name;
    char *icon;
    char label[256];
    char *fg;
    char *bg;
    int render_progress;
    double interval;
    OxWidgetUpdate update;
    OxWidgetClick click;
    void *ctx;
    double last_update;
    int dirty;
    char *cmd;
    pthread_t cmd_thread;
    int cmd_running;
};

OxWidget *ox_widget_new(const char *name, double interval) {
    OxWidget *w = calloc(1, sizeof(OxWidget));
    w->name = strdup(name);
    w->interval = interval;
    return w;
}

void ox_widget_destroy(OxWidget *w) {
    if (!w) return;
    if (w->cmd_running) pthread_join(w->cmd_thread, NULL);
    free(w->name);
    free(w->icon);
    free(w->fg);
    free(w->bg);
    free(w->cmd);
    free(w);
}

void ox_widget_set_update(OxWidget *w, OxWidgetUpdate update, void *ctx) {
    w->update = update;
    w->ctx = ctx;
}

void ox_widget_set_click(OxWidget *w, OxWidgetClick click) {
    w->click = click;
}

void ox_widget_set_icon(OxWidget *w, const char *icon) {
    free(w->icon);
    w->icon = icon ? strdup(icon) : NULL;
}

void ox_widget_set_label_text(OxWidget *w, const char *text) {
    if (text) {
        snprintf(w->label, sizeof(w->label), "%s", text);
        w->dirty = 1;
    } else {
        w->label[0] = '\0';
    }
}

void ox_widget_set_colors(OxWidget *w, const char *fg, const char *bg) {
    free(w->fg);
    free(w->bg);
    w->fg = fg ? strdup(fg) : NULL;
    w->bg = bg ? strdup(bg) : NULL;
}

void ox_widget_set_render_progress(OxWidget *w, int enable) {
    w->render_progress = enable;
}

static void *cmd_thread(void *arg) {
    OxWidget *w = arg;
    char buf[256] = {0};
    FILE *f = popen(w->cmd, "r");
    if (f) {
        if (fgets(buf, sizeof(buf), f)) {
            size_t n = strlen(buf);
            while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r'))
                buf[--n] = '\0';
        }
        pclose(f);
    }
    if (strcmp(w->label, buf) != 0) {
        snprintf(w->label, sizeof(w->label), "%s", buf);
        w->dirty = 1;
    }
    w->cmd_running = 0;
    return NULL;
}

static void run_cmd_async(OxWidget *w) {
    if (w->cmd_running) return;
    if (!w->cmd || !*w->cmd) return;
    w->cmd_running = 1;
    pthread_create(&w->cmd_thread, NULL, cmd_thread, w);
}

void ox_widget_update(OxWidget *w) {
    if (w->cmd && *w->cmd) {
        run_cmd_async(w);
    } else if (w->update) {
        char buf[256] = {0};
        w->update(w->ctx, buf, sizeof(buf));
        if (strcmp(w->label, buf) != 0) {
            snprintf(w->label, sizeof(w->label), "%s", buf);
            w->dirty = 1;
        }
    }
}

void ox_widget_do_click(OxWidget *w) {
    if (w && w->click) w->click(w->ctx);
}

const char *ox_widget_get_name(OxWidget *w) { return w->name; }
const char *ox_widget_get_label(OxWidget *w) { return w->label; }
const char *ox_widget_get_icon(OxWidget *w) { return w->icon; }
const char *ox_widget_get_fg(OxWidget *w) { return w->fg; }
const char *ox_widget_get_bg(OxWidget *w) { return w->bg; }
double ox_widget_get_interval(OxWidget *w) { return w->interval; }
double ox_widget_get_last_update(OxWidget *w) { return w->last_update; }
void ox_widget_set_last_update(OxWidget *w, double t) { w->last_update = t; }
int ox_widget_is_dirty(OxWidget *w) { return w->dirty; }
void ox_widget_clear_dirty(OxWidget *w) { w->dirty = 0; }
