// widget.c - Widget implementations

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "widget.h"

// Run a shell command and get output
static void run_cmd(const char *cmd, char *buf, size_t len) {
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
        // Remove trailing newline
        size_t n = strlen(buf);
        while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) {
            buf[--n] = '\0';
        }
    } else {
        buf[0] = '\0';
    }

    pclose(f);
}

// Widget create/destroy
Widget *widget_create(const char *name, const char *icon,
                      double interval, WidgetUpdate update,
                      WidgetClick click, void *ctx) {
    Widget *widget = calloc(1, sizeof(Widget));
    if (widget == NULL) {
        return NULL;
    }

    widget->name = strdup(name);
    widget->icon = icon ? strdup(icon) : NULL;
    widget->label = calloc(256, sizeof(char));
    widget->interval = interval;
    widget->update = update;
    widget->click = click;
    widget->ctx = ctx;

    return widget;
}

void widget_destroy(Widget *widget) {
    if (widget == NULL) {
        return;
    }
    free(widget->name);
    free(widget->icon);
    free(widget->label);
    free(widget);
}

void widget_update(Widget *widget) {
    if (widget->update != NULL) {
        widget->update(widget->ctx, widget->label, 256);
    }
}

// Built-in widget: time
static void update_time(void *ctx, char *buf, size_t len) {
    (void)ctx;
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    snprintf(buf, len, "%02d:%02d:%02d",
        tm->tm_hour, tm->tm_min, tm->tm_sec);
}

Widget *widget_time_create(void) {
    return widget_create("time", NULL, 1.0, update_time, NULL, NULL);
}

// Built-in widget: date
static void update_date(void *ctx, char *buf, size_t len) {
    (void)ctx;
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    const char *days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                            "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    snprintf(buf, len, "%s %s %d",
        days[tm->tm_wday], months[tm->tm_mon], tm->tm_mday);
}

Widget *widget_date_create(void) {
    return widget_create("date", NULL, 60.0, update_date, NULL, NULL);
}

// Built-in widget: cpu
static void update_cpu(void *ctx, char *buf, size_t len) {
    (void)ctx;
    FILE *f = fopen("/proc/stat", "r");
    if (f == NULL) {
        snprintf(buf, len, "??%%");
        return;
    }

    long user, nice, sys, idle;
    if (fscanf(f, "cpu %ld %ld %ld %ld", &user, &nice, &sys, &idle) == 4) {
        long total = user + nice + sys + idle;
        int usage = (total > 0) ? (int)(idle * 100 / total) : 0;
        snprintf(buf, len, "%d%%", 100 - usage);
    } else {
        snprintf(buf, len, "??%%");
    }

    fclose(f);
}

Widget *widget_cpu_create(void) {
    return widget_create("cpu", NULL, 1.0, update_cpu, NULL, NULL);
}

// Built-in widget: mem
static void update_mem(void *ctx, char *buf, size_t len) {
    (void)ctx;
    FILE *f = fopen("/proc/meminfo", "r");
    if (f == NULL) {
        snprintf(buf, len, "??MB");
        return;
    }

    long total = 0, avail = 0;
    char line[256];
    while (fgets(line, sizeof(line), f) != NULL) {
        if (strncmp(line, "MemTotal:", 9) == 0) {
            sscanf(line, "MemTotal: %ld kB", &total);
        } else if (strncmp(line, "MemAvailable:", 13) == 0) {
            sscanf(line, "MemAvailable: %ld kB", &avail);
        }
    }
    fclose(f);

    if (total > 0) {
        snprintf(buf, len, "%ldMB", (total - avail) / 1024);
    } else {
        snprintf(buf, len, "??MB");
    }
}

Widget *widget_mem_create(void) {
    return widget_create("mem", NULL, 1.0, update_mem, NULL, NULL);
}

// Built-in widget: disk
static void update_disk(void *ctx, char *buf, size_t len) {
    (void)ctx;
    run_cmd("df -h / | tail -1 | awk '{print $3\"/\"$2}'", buf, len);
}

Widget *widget_disk_create(void) {
    return widget_create("disk", NULL, 30.0, update_disk, NULL, NULL);
}

// Built-in widget: net
static void update_net(void *ctx, char *buf, size_t len) {
    (void)ctx;
    run_cmd("awk 'NR>2{rx+=$2;tx+=$10}END{printf\"%.0f/%.0fMB\",rx/1048576,tx/1048576}' /proc/net/dev",
        buf, len);
}

Widget *widget_net_create(void) {
    return widget_create("net", NULL, 1.0, update_net, NULL, NULL);
}

// Built-in widget: vol
static void update_vol(void *ctx, char *buf, size_t len) {
    (void)ctx;
    char vol[32] = {0};
    char muted[32] = {0};

    run_cmd("pactl get-sink-volume @DEFAULT_SINK@ 2>/dev/null | grep -oP '\\d+%' | head -1",
        vol, sizeof(vol));
    run_cmd("pactl get-sink-mute @DEFAULT_SINK@ 2>/dev/null | grep -o 'yes'",
        muted, sizeof(muted));

    if (vol[0] == '\0') {
        snprintf(buf, len, "??%%");
    } else if (strcmp(muted, "yes") == 0) {
        snprintf(buf, len, "Mute");
    } else {
        snprintf(buf, len, "%s", vol);
    }
}

Widget *widget_vol_create(void) {
    return widget_create("vol", NULL, 1.0, update_vol, NULL, NULL);
}

// Built-in widget: bright
static void update_bright(void *ctx, char *buf, size_t len) {
    (void)ctx;
    FILE *f_cur = fopen("/sys/class/backlight/intel_backlight/brightness", "r");
    FILE *f_max = fopen("/sys/class/backlight/intel_backlight/max_brightness", "r");

    if (f_cur == NULL || f_max == NULL) {
        snprintf(buf, len, "??%%");
        if (f_cur != NULL) fclose(f_cur);
        if (f_max != NULL) fclose(f_max);
        return;
    }

    int cur = 0, max = 100;
    fscanf(f_cur, "%d", &cur);
    fscanf(f_max, "%d", &max);
    fclose(f_cur);
    fclose(f_max);

    snprintf(buf, len, "%d%%", (max > 0) ? (cur * 100 / max) : 0);
}

Widget *widget_bright_create(void) {
    return widget_create("bright", NULL, 5.0, update_bright, NULL, NULL);
}

// Built-in widget: bat
static void update_bat(void *ctx, char *buf, size_t len) {
    (void)ctx;
    FILE *f_cap = fopen("/sys/class/power_supply/BAT0/capacity", "r");
    if (f_cap == NULL) {
        snprintf(buf, len, "AC");
        return;
    }

    int cap = 0;
    fscanf(f_cap, "%d", &cap);
    fclose(f_cap);

    FILE *f_st = fopen("/sys/class/power_supply/BAT0/status", "r");
    char status[32] = {0};
    if (f_st != NULL) {
        fscanf(f_st, "%31s", status);
        fclose(f_st);
    }

    if (strcmp(status, "Charging") == 0) {
        snprintf(buf, len, "%d%%+", cap);
    } else {
        snprintf(buf, len, "%d%%", cap);
    }
}

Widget *widget_bat_create(void) {
    return widget_create("bat", NULL, 30.0, update_bat, NULL, NULL);
}

// Built-in widget: load
static void update_load(void *ctx, char *buf, size_t len) {
    (void)ctx;
    FILE *f = fopen("/proc/loadavg", "r");
    if (f == NULL) {
        snprintf(buf, len, "??");
        return;
    }

    if (fscanf(f, "%255s", buf) != 1) {
        snprintf(buf, len, "??");
    }

    fclose(f);
}

Widget *widget_load_create(void) {
    return widget_create("load", NULL, 1.0, update_load, NULL, NULL);
}

// Built-in widget: uptime
static void update_uptime(void *ctx, char *buf, size_t len) {
    (void)ctx;
    FILE *f = fopen("/proc/uptime", "r");
    if (f == NULL) {
        snprintf(buf, len, "??");
        return;
    }

    double up = 0;
    if (fscanf(f, "%lf", &up) == 1) {
        int d = (int)up / 86400;
        int h = ((int)up % 86400) / 3600;
        int m = ((int)up % 3600) / 60;
        if (d > 0) {
            snprintf(buf, len, "%dd %dh %dm", d, h, m);
        } else {
            snprintf(buf, len, "%dh %dm", h, m);
        }
    } else {
        snprintf(buf, len, "??");
    }

    fclose(f);
}

Widget *widget_uptime_create(void) {
    return widget_create("uptime", NULL, 60.0, update_uptime, NULL, NULL);
}

// External widget (runs a command)
static void update_cmd(void *ctx, char *buf, size_t len) {
    const char *cmd = (const char *)ctx;
    if (cmd != NULL) {
        run_cmd(cmd, buf, len);
    } else {
        buf[0] = '\0';
    }
}

static void click_cmd(void *ctx) {
    const char *cmd = (const char *)ctx;
    if (cmd != NULL) {
        if (fork() == 0) {
            setsid();
            execl("/bin/sh", "sh", "-c", cmd, NULL);
            _exit(1);
        }
    }
}

Widget *widget_cmd_create(const char *name, const char *icon,
                          const char *cmd, double interval) {
    Widget *widget = widget_create(name, icon, interval,
        update_cmd, click_cmd, (void *)cmd);
    return widget;
}
