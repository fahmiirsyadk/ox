#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include "ox.h"

#define C_FG    "#F47193"
#define C_BG    "#040403"
#define C_SEP   "#666666"
#define C_ACC   "#FFB86C"
#define C_DIM   "#b3afc2"
#define C_RED   "#FF0000"
#define NWSP    5
#define ICON_SZ 20

typedef struct {
    OxWindow *win;
    OxWidget *widgets[16];
    int count;
    int height, padding, width;
    const char *fg, *bg, *sep;
} Bar;

static void bar_add(Bar *b, OxWidget *w) { b->widgets[b->count++] = w; }

static int get_current_desktop(void) {
    Display *dpy = ox_display();
    Atom actual; int format;
    unsigned long nitems, bytes_after;
    unsigned char *prop = NULL;
    Atom a = XInternAtom(dpy, "_NET_CURRENT_DESKTOP", True);
    if (a == None) return 0;
    if (XGetWindowProperty(dpy, RootWindow(dpy, ox_screen()), a,
            0, 1, False, XA_CARDINAL, &actual, &format, &nitems, &bytes_after,
            &prop) == Success && prop) {
        int d = *(unsigned long *)prop; XFree(prop); return d;
    }
    return 0;
}

static void wsp_update(void *ctx, char *buf, size_t len) {
    snprintf(buf, len, "%d", (int)(long)ctx + 1);
}

static void vol_update(void *ctx, char *buf, size_t len) {
    (void)ctx;
    FILE *f = popen("pactl get-sink-volume @DEFAULT_SINK@ 2>/dev/null | grep -oP '\\d+%' | head -1 | grep -oP '\\d+'", "r");
    if (!f) { snprintf(buf, len, "??%%"); return; }
    if (fgets(buf, len, f)) {
        size_t n = strlen(buf);
        while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r')) buf[--n] = '\0';
    } else snprintf(buf, len, "??%%");
    pclose(f);
}

static void bat_update(void *ctx, char *buf, size_t len) {
    (void)ctx;
    FILE *f = fopen("/sys/class/power_supply/BAT0/capacity", "r");
    if (!f) { snprintf(buf, len, "AC"); return; }
    int pct = 0; fscanf(f, "%d", &pct); fclose(f);
    FILE *f2 = fopen("/sys/class/power_supply/BAT0/status", "r");
    char s[32] = "";
    if (f2) { fgets(s, sizeof(s), f2); fclose(f2); }
    size_t n = strlen(s);
    while (n > 0 && (s[n-1] == '\n' || s[n-1] == '\r')) s[--n] = '\0';
    if (strcmp(s, "Charging") == 0) snprintf(buf, len, "%d%%+", pct);
    else if (strcmp(s, "Full") == 0) snprintf(buf, len, "Full");
    else snprintf(buf, len, "%d%%", pct);
}

static volatile int show_power = 0;
static void power_click(void *ctx) { (void)ctx; show_power = 1; }
static void power_shutdown(void *ctx) { (void)ctx; system("shutdown -h now"); }
static void power_reboot(void *ctx) { (void)ctx; system("reboot"); }
static void power_exit(void *ctx) { (void)ctx; kill(getppid(), SIGTERM); }
static void wsp_click(void *ctx) {
    char cmd[32]; snprintf(cmd, sizeof(cmd), "xdotool key super+%d", (int)(long)ctx + 1);
    system(cmd);
}

static void render_left(OxWindow *win, void *ctx) {
    Bar *b = ctx;
    ox_draw_rect(win, 0, 0, b->width, b->height, b->bg);
    ox_draw_xpm(win, b->padding, (b->height - 42) / 2, "examples/xmobar/nix-22.xpm");
}

static void render_center(OxWindow *win, void *ctx) {
    Bar *b = ctx;
    int cy = b->height / 2 + ox_text_width(win, "X") / 3;
    ox_draw_rect(win, 0, 0, b->width, b->height, b->bg);
    int cx = b->padding;
    int cur_d = get_current_desktop();
    for (int i = 0; i < b->count; i++) {
        const char *label = ox_widget_get_label(b->widgets[i]);
        const char *color = (i == cur_d) ? C_FG : C_DIM;
        ox_draw_text(win, cx, cy, label, color);
        cx += ox_text_width(win, label) + b->padding;
    }
    (void)b;
}

static void render_right(OxWindow *win, void *ctx) {
    Bar *b = ctx;
    int cy = b->height / 2 + ox_text_width(win, "X") / 3;
    ox_draw_rect(win, 0, 0, b->width, b->height, b->bg);
    int cx = b->padding;
    const char *icons[] = { "examples/xmobar/cpu_20.xpm",
                             "examples/xmobar/harddisk-icon_20.xpm",
                             "examples/xmobar/net_up_20.xpm" };
    for (int i = 0; i < b->count; i++) {
        if (i > 0) {
            ox_draw_text(win, cx, cy, "|", b->sep);
            cx += ox_text_width(win, "|") + b->padding;
        }
        ox_draw_xpm(win, cx, (b->height - ICON_SZ) / 2, icons[i]);
        cx += ICON_SZ + b->padding;
        const char *label = ox_widget_get_label(b->widgets[i]);
        const char *color = ox_widget_get_fg(b->widgets[i]);
        if (!color) color = b->fg;
        ox_draw_text(win, cx, cy, label, color);
        cx += ox_text_width(win, label) + b->padding;
    }
    (void)b;
}

int main(void) {
    ox_init();
    int sw = DisplayWidth(ox_display(), ox_screen());
    int h = 28, pad = 8;
    const char *font = "monospace:size=11";

    /* ── left: nix icon ── */
    int icon_w = 46;
    Bar left = { .height = h, .padding = pad, .fg = C_FG, .bg = C_BG, .sep = C_SEP,
                 .width = icon_w + pad * 2 };

    /* ── center: workspaces 1-5 ── */
    Bar center = { .height = h, .padding = pad, .fg = C_DIM, .bg = C_BG, .sep = C_SEP };
    OxWidget *wsp[NWSP];
    for (int i = 0; i < NWSP; i++) {
        wsp[i] = ox_widget_new("wsp", 0.2);
        ox_widget_set_update(wsp[i], wsp_update, (void *)(long)i);
        ox_widget_set_click(wsp[i], wsp_click);
        bar_add(&center, wsp[i]);
    }

    /* ── right: vol + bat + power ── */
    Bar right = { .height = h, .padding = pad, .fg = C_DIM, .bg = C_BG, .sep = C_SEP };
    OxWidget *w_vol = ox_widget_new("vol", 5.0);
    ox_widget_set_colors(w_vol, C_ACC, NULL);
    ox_widget_set_update(w_vol, vol_update, NULL);
    bar_add(&right, w_vol);

    OxWidget *w_bat = ox_widget_new("bat", 50.0);
    ox_widget_set_update(w_bat, bat_update, NULL);
    bar_add(&right, w_bat);

    OxWidget *w_power = ox_widget_new("power", 0);
    ox_widget_set_colors(w_power, C_RED, NULL);
    ox_widget_set_label_text(w_power, "off");
    ox_widget_set_click(w_power, power_click);
    bar_add(&right, w_power);

    /* ── popup ── */
    Bar popup = { .height = h, .padding = pad, .fg = C_FG, .bg = C_BG, .sep = C_SEP };
    OxWidget *w_shutdown = ox_widget_new("shutdown", 0);
    ox_widget_set_label_text(w_shutdown, "Shutdown");
    ox_widget_set_click(w_shutdown, power_shutdown);
    bar_add(&popup, w_shutdown);
    OxWidget *w_reboot = ox_widget_new("reboot", 0);
    ox_widget_set_label_text(w_reboot, "Reboot");
    ox_widget_set_click(w_reboot, power_reboot);
    bar_add(&popup, w_reboot);
    OxWidget *w_exit = ox_widget_new("exit", 0);
    ox_widget_set_label_text(w_exit, "Exit");
    ox_widget_set_click(w_exit, power_exit);
    bar_add(&popup, w_exit);

    /* ── update all widgets first ── */
    for (int i = 0; i < center.count; i++) ox_widget_update(center.widgets[i]);
    for (int i = 0; i < right.count; i++) ox_widget_update(right.widgets[i]);

    /* ── measure ── */
    OxWindow *tmp = ox_window_new(-100, -100, 1, 1);
    ox_window_set_font(tmp, font);
    int cw = pad;
    for (int i = 0; i < center.count; i++)
        cw += ox_text_width(tmp, ox_widget_get_label(center.widgets[i])) + pad;
    int rw = pad;
    for (int i = 0; i < right.count; i++) {
        if (i > 0) rw += ox_text_width(tmp, "|") + pad;
        rw += ICON_SZ + pad;
        rw += ox_text_width(tmp, ox_widget_get_label(right.widgets[i])) + pad;
    }
    int pw = pad;
    for (int i = 0; i < popup.count; i++)
        pw += ox_text_width(tmp, ox_widget_get_label(popup.widgets[i])) + pad;
    ox_window_destroy(tmp);

    center.width = cw;
    right.width = rw;

    /* ── create windows ── */
    left.win = ox_window_new(0, 0, left.width, h);
    ox_window_set_bg(left.win, left.bg);
    ox_window_set_font(left.win, font);
    ox_window_set_render(left.win, render_left, &left);

    center.win = ox_window_new((sw - cw) / 2, 0, cw, h);
    ox_window_set_bg(center.win, center.bg);
    ox_window_set_font(center.win, font);
    ox_window_set_render(center.win, render_center, &center);

    right.win = ox_window_new(sw - rw, 0, rw, h);
    ox_window_set_bg(right.win, right.bg);
    ox_window_set_font(right.win, font);
    ox_window_set_strut(right.win, h, 0, 0, 0);
    ox_window_set_render(right.win, render_right, &right);

    popup.win = ox_window_new(sw - pw, 0, pw, h);
    ox_window_set_bg(popup.win, popup.bg);
    ox_window_set_font(popup.win, font);

    ox_window_show(left.win);
    ox_window_show(center.win);
    ox_window_show(right.win);

    /* ── initial render ── */
    render_left(left.win, &left);
    render_center(center.win, &center);
    render_right(right.win, &right);

    int last_desktop = get_current_desktop();

    struct timespec ts = { .tv_sec = 0, .tv_nsec = 100000000 };
    struct timespec now_ts;
    clock_gettime(CLOCK_MONOTONIC, &now_ts);

    for (;;) {
        clock_gettime(CLOCK_MONOTONIC, &now_ts);
        double cur = now_ts.tv_sec + now_ts.tv_nsec / 1e9;

        if (show_power) { show_power = 0; ox_window_show(popup.win); }

        /* update widgets */
        for (int i = 0; i < right.count; i++) {
            OxWidget *w = right.widgets[i];
            if (ox_widget_get_interval(w) > 0 &&
                cur - ox_widget_get_last_update(w) >= ox_widget_get_interval(w)) {
                ox_widget_update(w);
                ox_widget_set_last_update(w, cur);
            }
        }

        /* check if any widget changed */
        int any_dirty = 0;
        for (int i = 0; i < right.count; i++)
            if (ox_widget_is_dirty(right.widgets[i])) any_dirty = 1;

        /* workspace change */
        int cur_d = get_current_desktop();
        if (cur_d != last_desktop) { last_desktop = cur_d; any_dirty = 1; }

        /* redraw only if something changed */
        if (any_dirty) {
            render_center(center.win, &center);
            render_right(right.win, &right);
            for (int i = 0; i < center.count; i++) ox_widget_clear_dirty(center.widgets[i]);
            for (int i = 0; i < right.count; i++) ox_widget_clear_dirty(right.widgets[i]);
        }

        /* handle events */
        Display *dpy = ox_display();
        while (XPending(dpy) > 0) {
            XEvent ev;
            XNextEvent(dpy, &ev);
            if (ev.type == Expose) {
                render_left(left.win, &left);
                render_center(center.win, &center);
                render_right(right.win, &right);
            }
            if (ev.type == ButtonPress) {
                if (ev.xbutton.window == ox_window_handle(popup.win)) {
                    int ccx = popup.padding;
                    for (int i = 0; i < popup.count; i++) {
                        int ww = ox_text_width(popup.win, ox_widget_get_label(popup.widgets[i])) + popup.padding;
                        if (ev.xbutton.x >= ccx && ev.xbutton.x < ccx + ww) {
                            ox_widget_do_click(popup.widgets[i]);
                            ox_window_hide(popup.win);
                            break;
                        }
                        ccx += ww + popup.padding;
                    }
                }
                if (ev.xbutton.window == ox_window_handle(center.win)) {
                    int ccx = center.padding;
                    for (int i = 0; i < center.count; i++) {
                        char num[2] = { '1' + i, 0 };
                        int ww = ox_text_width(center.win, num) + center.padding;
                        if (ev.xbutton.x >= ccx && ev.xbutton.x < ccx + ww) {
                            ox_widget_do_click(center.widgets[i]);
                            break;
                        }
                        ccx += ww + center.padding;
                    }
                }
                if (ev.xbutton.window == ox_window_handle(right.win)) {
                    int ccx = right.padding;
                    for (int i = 0; i < right.count; i++) {
                        int ww = ICON_SZ + right.padding +
                                 ox_text_width(right.win, ox_widget_get_label(right.widgets[i])) + right.padding;
                        if (i > 0) ww += ox_text_width(right.win, "|") + right.padding;
                        if (ev.xbutton.x >= ccx && ev.xbutton.x < ccx + ww) {
                            ox_widget_do_click(right.widgets[i]);
                            break;
                        }
                        ccx += ww;
                    }
                }
            }
        }

        XFlush(dpy);
        nanosleep(&ts, NULL);
    }

    return 0;
}
