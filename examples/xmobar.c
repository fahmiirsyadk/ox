#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/types.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <alsa/asoundlib.h>
#include "ox.h"

#define C_FG    "#F47193"
#define C_BG    "#040403"
#define C_SEP   "#666666"
#define C_ACC   "#FFB86C"
#define C_DIM   "#b3afc2"
#define NWSP    5
#define ICON_SZ 20

typedef struct {
    int x, width;
} WidgetLayout;

typedef struct {
    OxWindow *win;
    OxWidget *widgets[16];
    WidgetLayout layout[16];
    int count;
    int height, padding, width;
    const char *fg, *bg, *sep;
} Bar;

typedef struct {
    Bar left, center, right;
    int current_desktop;
    int needs_redraw;
    double last_wsp_check;
    char exe_dir[PATH_MAX];
    char bat_path[PATH_MAX];
} XMBState;

static void run_cmd(const char *cmd) {
    pid_t pid = fork();
    if (pid == 0) {
        setsid();
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
        execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
        _exit(1);
    }
}

static void resolve_exe_dir(XMBState *s) {
    ssize_t n = readlink("/proc/self/exe", s->exe_dir, sizeof(s->exe_dir) - 1);
    if (n > 0) {
        s->exe_dir[n] = '\0';
        char *slash = strrchr(s->exe_dir, '/');
        if (slash) *slash = '\0';
    }
}

static const char *xpm_path(XMBState *s, const char *name) {
    static char buf[PATH_MAX];
    snprintf(buf, sizeof(buf), "%s/examples/xmobar/%s", s->exe_dir, name);
    return buf;
}

static void detect_battery(XMBState *s) {
    DIR *d = opendir("/sys/class/power_supply");
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "/sys/class/power_supply/%s/type", e->d_name);
        FILE *f = fopen(path, "r");
        if (!f) continue;
        char type[32] = "";
        if (fgets(type, sizeof(type), f)) {
            size_t n = strlen(type);
            while (n > 0 && (type[n-1] == '\n' || type[n-1] == '\r')) type[--n] = '\0';
        }
        fclose(f);
        if (strcmp(type, "Battery") == 0) {
            snprintf(s->bat_path, sizeof(s->bat_path),
                     "/sys/class/power_supply/%s", e->d_name);
            break;
        }
    }
    closedir(d);
}

static int get_current_desktop(void) {
    Display *dpy = ox_display();
    Atom actual_type;
    int actual_format;
    unsigned long nitems, bytes_after;
    unsigned char *prop = NULL;
    Atom atom = XInternAtom(dpy, "_NET_CURRENT_DESKTOP", True);
    if (atom == None) return 0;
    if (XGetWindowProperty(dpy, RootWindow(dpy, ox_screen()), atom,
            0, 1, False, XA_CARDINAL, &actual_type, &actual_format,
            &nitems, &bytes_after, &prop) == Success && prop != NULL) {
        int desktop = 0;
        if (actual_format == 32 && nitems > 0)
            desktop = (int)(*(long *)prop);
        XFree(prop);
        return desktop;
    }
    if (prop) XFree(prop);
    return 0;
}

static void bar_add(Bar *b, OxWidget *w) { b->widgets[b->count++] = w; }

static void wsp_update(void *ctx, char *buf, size_t len) {
    (void)ctx;
    int d = get_current_desktop();
    snprintf(buf, len, "%d", d + 1);
}

static void vol_update(void *ctx, char *buf, size_t len) {
    (void)ctx;
    snd_mixer_t *handle;
    snd_mixer_selem_id_t *sid;
    if (snd_mixer_open(&handle, 0) < 0) { snprintf(buf, len, "??%%"); return; }
    snd_mixer_attach(handle, "default");
    snd_mixer_selem_register(handle, NULL, NULL);
    snd_mixer_load(handle);
    snd_mixer_selem_id_alloca(&sid);
    snd_mixer_selem_id_set_index(sid, 0);
    snd_mixer_selem_id_set_name(sid, "Master");
    snd_mixer_elem_t *elem = snd_mixer_find_selem(handle, sid);
    if (!elem) { snd_mixer_close(handle); snprintf(buf, len, "??%%"); return; }
    long min, max, val;
    snd_mixer_selem_get_playback_volume_range(elem, &min, &max);
    snd_mixer_selem_get_playback_volume(elem, 0, &val);
    int pct = (int)((val - min) * 100 / (max - min));
    snd_mixer_close(handle);
    snprintf(buf, len, "%d%%", pct);
}

static void bat_update(void *ctx, char *buf, size_t len) {
    XMBState *s = ctx;
    if (s->bat_path[0] == '\0') { snprintf(buf, len, "AC"); return; }
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/capacity", s->bat_path);
    FILE *f = fopen(path, "r");
    if (!f) { snprintf(buf, len, "AC"); return; }
    int pct = 0;
    fscanf(f, "%d", &pct);
    fclose(f);
    snprintf(path, sizeof(path), "%s/status", s->bat_path);
    FILE *f2 = fopen(path, "r");
    char st[32] = "";
    if (f2) { fgets(st, sizeof(st), f2); fclose(f2); }
    size_t n = strlen(st);
    while (n > 0 && (st[n-1] == '\n' || st[n-1] == '\r')) st[--n] = '\0';
    if (strcmp(st, "Charging") == 0) snprintf(buf, len, "%d%%+", pct);
    else if (strcmp(st, "Full") == 0) snprintf(buf, len, "Full");
    else snprintf(buf, len, "%d%%", pct);
}

static void clock_update(void *ctx, char *buf, size_t len) {
    (void)ctx;
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    strftime(buf, len, "%H:%M", t);
}

static void wsp_click_0(void *ctx) { XMBState *s = ctx; s->current_desktop = 0; s->needs_redraw = 1; run_cmd("xdotool key super+1"); }
static void wsp_click_1(void *ctx) { XMBState *s = ctx; s->current_desktop = 1; s->needs_redraw = 1; run_cmd("xdotool key super+2"); }
static void wsp_click_2(void *ctx) { XMBState *s = ctx; s->current_desktop = 2; s->needs_redraw = 1; run_cmd("xdotool key super+3"); }
static void wsp_click_3(void *ctx) { XMBState *s = ctx; s->current_desktop = 3; s->needs_redraw = 1; run_cmd("xdotool key super+4"); }
static void wsp_click_4(void *ctx) { XMBState *s = ctx; s->current_desktop = 4; s->needs_redraw = 1; run_cmd("xdotool key super+5"); }

static OxWidgetClick wsp_click_fns[NWSP] = {
    wsp_click_0, wsp_click_1, wsp_click_2, wsp_click_3, wsp_click_4
};

static void render_left(OxWindow *win, void *ctx) {
    XMBState *s = ctx;
    ox_draw_rect(win, 0, 0, s->left.width, s->left.height, s->left.bg);
    ox_draw_xpm(win, s->left.padding, (s->left.height - 42) / 2, xpm_path(s, "nix-22.xpm"));
}

static void render_center(OxWindow *win, void *ctx) {
    XMBState *s = ctx;
    Bar *b = &s->center;
    int cy = b->height / 2 + ox_text_width(win, "X") / 3;
    ox_draw_rect(win, 0, 0, b->width, b->height, b->bg);
    int cx = b->padding;
    for (int i = 0; i < b->count; i++) {
        const char *label = ox_widget_get_label(b->widgets[i]);
        int ww = ox_text_width(win, label) + b->padding;
        b->layout[i].x = cx;
        b->layout[i].width = ww;
        const char *color = (i == s->current_desktop) ? C_FG : C_DIM;
        ox_draw_text(win, cx, cy, label, color);
        cx += ww;
    }
}

static void render_right(OxWindow *win, void *ctx) {
    XMBState *s = ctx;
    Bar *b = &s->right;
    int cy = b->height / 2 + ox_text_width(win, "X") / 3;
    ox_draw_rect(win, 0, 0, b->width, b->height, b->bg);
    int cx = b->padding;
    static const char *icons[] = { "cpu_20.xpm", "harddisk-icon_20.xpm", "net_up_20.xpm" };
    for (int i = 0; i < b->count; i++) {
        int ww = 0;
        if (i > 0) {
            ox_draw_text(win, cx, cy, "|", b->sep);
            ww += ox_text_width(win, "|") + b->padding;
        }
        if (i < 3) ox_draw_xpm(win, cx, (b->height - ICON_SZ) / 2, xpm_path(s, icons[i]));
        ww += ICON_SZ + b->padding;
        const char *label = ox_widget_get_label(b->widgets[i]);
        const char *color = ox_widget_get_fg(b->widgets[i]);
        if (!color) color = b->fg;
        ox_draw_text(win, cx + ICON_SZ + b->padding, cy, label, color);
        ww += ox_text_width(win, label) + b->padding;
        b->layout[i].x = cx;
        b->layout[i].width = ww;
        cx += ww;
    }
}

static int bar_hit_test(Bar *b, int x) {
    for (int i = 0; i < b->count; i++)
        if (x >= b->layout[i].x && x < b->layout[i].x + b->layout[i].width)
            return i;
    return -1;
}

static void on_timeout(OxMain *m, double now) {
    XMBState *s = m->ctx;

    /* slow poll: sync workspace from keyboard */
    if (now - s->last_wsp_check > 1.0) {
        s->last_wsp_check = now;
        int cd = get_current_desktop();
        if (cd != s->current_desktop) {
            s->current_desktop = cd;
            s->needs_redraw = 1;
        }
    }

    /* update right widgets */
    for (int i = 0; i < s->right.count; i++) {
        OxWidget *w = s->right.widgets[i];
        if (ox_widget_get_interval(w) > 0 &&
            now - ox_widget_get_last_update(w) >= ox_widget_get_interval(w)) {
            ox_widget_update(w);
            ox_widget_set_last_update(w, now);
        }
        /* check if async cmd produced output */
        int fd = ox_widget_get_fd(w);
        if (fd >= 0) {
            /* add to extra_fds so select() monitors it */
            int found = 0;
            for (int j = 0; j < m->extra_fd_count; j++)
                if (m->extra_fds[j] == fd) { found = 1; break; }
            if (!found && m->extra_fd_count < 8)
                m->extra_fds[m->extra_fd_count++] = fd;
        }
    }

    /* redraw if dirty */
    int any_dirty = s->needs_redraw;
    s->needs_redraw = 0;
    for (int i = 0; i < s->right.count; i++)
        if (ox_widget_is_dirty(s->right.widgets[i])) any_dirty = 1;
    /* also check if any widget's async fd is ready */
    for (int i = 0; i < s->right.count; i++) {
        OxWidget *w = s->right.widgets[i];
        int fd = ox_widget_get_fd(w);
        if (fd >= 0) {
            /* try non-blocking read */
            fd_set test;
            FD_ZERO(&test);
            FD_SET(fd, &test);
            struct timeval tv = {0, 0};
            if (select(fd + 1, &test, NULL, NULL, &tv) > 0) {
                ox_widget_read(w);
                if (ox_widget_is_dirty(w)) any_dirty = 1;
            }
        }
    }

    if (any_dirty) {
        render_center(s->center.win, s);
        render_right(s->right.win, s);
        for (int i = 0; i < s->center.count; i++) ox_widget_clear_dirty(s->center.widgets[i]);
        for (int i = 0; i < s->right.count; i++) ox_widget_clear_dirty(s->right.widgets[i]);
    }
}

static void on_event(OxMain *m, XEvent *ev) {
    XMBState *s = m->ctx;
    if (ev->type == Expose) {
        render_left(s->left.win, s);
        render_center(s->center.win, s);
        render_right(s->right.win, s);
    }
    if (ev->type == ButtonPress) {
        if (ev->xbutton.window == ox_window_handle(s->center.win)) {
            int idx = bar_hit_test(&s->center, ev->xbutton.x);
            if (idx >= 0) ox_widget_do_click(s->center.widgets[idx]);
        }
        if (ev->xbutton.window == ox_window_handle(s->right.win)) {
            int idx = bar_hit_test(&s->right, ev->xbutton.x);
            if (idx >= 0) ox_widget_do_click(s->right.widgets[idx]);
        }
    }
}

int main(void) {
    XMBState state = {0};
    XMBState *s = &state;

    resolve_exe_dir(s);
    detect_battery(s);
    ox_init();

    Display *dpy = ox_display();
    int screen = ox_screen();
    int sw = DisplayWidth(dpy, screen);
    int h = 28, pad = 8;
    const char *font = "monospace:size=11";

    /* ── left: nix icon ── */
    s->left = (Bar){ .height = h, .padding = pad, .fg = C_FG, .bg = C_BG, .sep = C_SEP,
                     .width = 46 + pad * 2 };

    /* ── center: workspaces ── */
    s->center = (Bar){ .height = h, .padding = pad, .fg = C_DIM, .bg = C_BG, .sep = C_SEP };
    for (int i = 0; i < NWSP; i++) {
        OxWidget *w = ox_widget_new("wsp", 0.2);
        ox_widget_set_update(w, wsp_update, s);
        ox_widget_set_click(w, wsp_click_fns[i]);
        bar_add(&s->center, w);
    }

    /* ── right: vol + bat + clock ── */
    s->right = (Bar){ .height = h, .padding = pad, .fg = C_DIM, .bg = C_BG, .sep = C_SEP };
    OxWidget *w_vol = ox_widget_new("vol", 5.0);
    ox_widget_set_colors(w_vol, C_ACC, NULL);
    ox_widget_set_update(w_vol, vol_update, NULL);
    bar_add(&s->right, w_vol);

    OxWidget *w_bat = ox_widget_new("bat", 50.0);
    ox_widget_set_update(w_bat, bat_update, s);
    bar_add(&s->right, w_bat);

    OxWidget *w_clock = ox_widget_new("clock", 1.0);
    ox_widget_set_colors(w_clock, C_FG, NULL);
    ox_widget_set_update(w_clock, clock_update, NULL);
    bar_add(&s->right, w_clock);

    /* ── update all widgets first ── */
    for (int i = 0; i < s->center.count; i++) ox_widget_update(s->center.widgets[i]);
    for (int i = 0; i < s->right.count; i++) ox_widget_update(s->right.widgets[i]);

    /* ── measure ── */
    OxWindow *tmp = ox_window_new(-100, -100, 1, 1);
    ox_window_set_font(tmp, font);
    int cw = pad;
    for (int i = 0; i < s->center.count; i++)
        cw += ox_text_width(tmp, ox_widget_get_label(s->center.widgets[i])) + pad;
    int rw = pad;
    for (int i = 0; i < s->right.count; i++) {
        if (i > 0) rw += ox_text_width(tmp, "|") + pad;
        rw += ICON_SZ + pad;
        rw += ox_text_width(tmp, ox_widget_get_label(s->right.widgets[i])) + pad;
    }
    ox_window_destroy(tmp);
    s->center.width = cw;
    s->right.width = rw;

    /* ── create windows ── */
    s->left.win = ox_window_new(0, 0, s->left.width, h);
    ox_window_set_bg(s->left.win, s->left.bg);
    ox_window_set_font(s->left.win, font);

    s->center.win = ox_window_new((sw - cw) / 2, 0, cw, h);
    ox_window_set_bg(s->center.win, s->center.bg);
    ox_window_set_font(s->center.win, font);

    s->right.win = ox_window_new(sw - rw, 0, rw, h);
    ox_window_set_bg(s->right.win, s->right.bg);
    ox_window_set_font(s->right.win, font);
    ox_window_set_strut(s->right.win, h, 0, 0, 0);

    ox_window_show(s->left.win);
    ox_window_show(s->center.win);
    ox_window_show(s->right.win);

    /* ── initial render ── */
    render_left(s->left.win, s);
    render_center(s->center.win, s);
    render_right(s->right.win, s);

    s->current_desktop = get_current_desktop();

    /* ── run ── */
    OxMain loop = {
        .on_event = on_event,
        .on_timeout = on_timeout,
        .ctx = s,
        .ipc_fd = -1,
        .timeout_ms = 100,
    };
    ox_main(&loop);

    ox_window_destroy(s->left.win);
    ox_window_destroy(s->center.win);
    ox_window_destroy(s->right.win);
    ox_cleanup();
    return 0;
}
