#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "oxbar.h"

Widget *widget_create(const char *name, const char *icon,
                      double interval, WidgetUpdate update, void *ctx)
{
    Widget *w = calloc(1, sizeof(Widget));
    if (!w) return NULL;
    w->name = strdup(name);
    w->icon = icon ? strdup(icon) : NULL;
    w->label = calloc(256, sizeof(char));
    w->interval = interval;
    w->update = update;
    w->ctx = ctx;
    return w;
}

void widget_destroy(Widget *w)
{
    if (!w) return;
    free(w->name);
    free(w->icon);
    free(w->label);
    free(w->click_cmd);
    free(w->scroll_up);
    free(w->scroll_down);
    free(w->fg);
    free(w->bg);
    free(w);
}

void widget_update(Widget *w)
{
    if (w->update)
        w->update(w->ctx, w->label, 256);
}

void run_cmd(const char *cmd, char *buf, size_t len)
{
    if (!cmd || !*cmd) { buf[0] = '\0'; return; }
    FILE *f = popen(cmd, "r");
    if (!f) { buf[0] = '\0'; return; }
    if (fgets(buf, len, f)) {
        size_t n = strlen(buf);
        while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r'))
            buf[--n] = '\0';
    } else {
        buf[0] = '\0';
    }
    pclose(f);
}

void update_cmd(void *ctx, char *buf, size_t len)
{
    const char *cmd = (const char *)ctx;
    if (cmd) run_cmd(cmd, buf, len);
    else buf[0] = '\0';
}

Widget *widget_cmd_create(const char *name, const char *icon,
                          const char *cmd, double interval)
{
    return widget_create(name, icon, interval, update_cmd,
        cmd ? strdup(cmd) : NULL);
}
