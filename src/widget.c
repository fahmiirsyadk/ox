#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "widget.h"

static void run_cmd(const char *cmd, char *buf, size_t len)
{
    if (cmd == NULL || *cmd == '\0') {
        buf[0] = '\0';
        return;
    }

    FILE *f = popen(cmd, "r");
    if (f == NULL) {
        buf[0] = '\0';
        return;
    }

    if (fgets(buf, len, f) != NULL) {
        size_t n = strlen(buf);
        while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r'))
            buf[--n] = '\0';
    } else {
        buf[0] = '\0';
    }

    pclose(f);
}

Widget *widget_create(const char *name, const char *icon,
                      double interval, WidgetUpdate update, void *ctx)
{
    Widget *widget = calloc(1, sizeof(Widget));
    if (widget == NULL)
        return NULL;

    widget->name = strdup(name);
    widget->icon = icon ? strdup(icon) : NULL;
    widget->label = calloc(256, sizeof(char));
    widget->interval = interval;
    widget->update = update;
    widget->ctx = ctx;

    return widget;
}

void widget_destroy(Widget *widget)
{
    if (widget == NULL)
        return;
    free(widget->name);
    free(widget->icon);
    free(widget->label);
    free(widget->click_cmd);
    free(widget);
}

void widget_update(Widget *widget)
{
    if (widget->update != NULL)
        widget->update(widget->ctx, widget->label, 256);
}

static void update_time(void *ctx, char *buf, size_t len)
{
    (void)ctx;
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    snprintf(buf, len, "%02d:%02d:%02d",
        tm->tm_hour, tm->tm_min, tm->tm_sec);
}

Widget *widget_time_create(void)
{
    return widget_create("time", NULL, 1.0, update_time, NULL);
}

static void update_cmd(void *ctx, char *buf, size_t len)
{
    const char *cmd = (const char *)ctx;
    if (cmd != NULL)
        run_cmd(cmd, buf, len);
    else
        buf[0] = '\0';
}

Widget *widget_cmd_create(const char *name, const char *icon,
                          const char *cmd, double interval)
{
    return widget_create(name, icon, interval,
        update_cmd, (void *)cmd);
}
