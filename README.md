# ox

A minimal C toolkit for building X11 status bars and widgets.

No config files. Each program is self-contained — you write C, not config.

## Structure

```
include/ox.h       Core API
src/
  widget.c         Widget lifecycle
  draw.c           Rendering: text, rect, xpm, window
  loop.c           Event loop
examples/
  simple-bar.c     Single bar
  multi-bar.c      Left/center/right bars
  vertical-bar.c   Sidebar
  osd.c            OSD overlay
  xmobar.c         Full xmobar-style bar
```

## Build

```bash
make
```

### Dependencies

- libx11, libxft, freetype2, fontconfig, libxpm

```bash
# Arch
sudo pacman -S libx11 libxft freetype2 fontconfig libxpm

# Debian/Ubuntu
sudo apt install libx11-dev libxft-dev libfreetype-dev libfontconfig1-dev libxpm-dev
```

## API

### Widget

```c
OxWidget *ox_widget_new(const char *name, double interval);
void ox_widget_destroy(OxWidget *w);
void ox_widget_set_update(OxWidget *w, OxWidgetUpdate update, void *ctx);
void ox_widget_set_click(OxWidget *w, OxWidgetClick click);
void ox_widget_set_icon(OxWidget *w, const char *icon);
void ox_widget_set_label_text(OxWidget *w, const char *text);
void ox_widget_set_colors(OxWidget *w, const char *fg, const char *bg);
void ox_widget_set_render_progress(OxWidget *w, int enable);
void ox_widget_update(OxWidget *w);
void ox_widget_do_click(OxWidget *w);

const char *ox_widget_get_name(OxWidget *w);
const char *ox_widget_get_label(OxWidget *w);
const char *ox_widget_get_icon(OxWidget *w);
const char *ox_widget_get_fg(OxWidget *w);
const char *ox_widget_get_bg(OxWidget *w);
double ox_widget_get_interval(OxWidget *w);
double ox_widget_get_last_update(OxWidget *w);
void ox_widget_set_last_update(OxWidget *w, double t);
```

### Window

```c
OxWindow *ox_window_new(int x, int y, int w, int h);
void ox_window_destroy(OxWindow *win);
void ox_window_set_bg(OxWindow *win, const char *color);
void ox_window_set_font(OxWindow *win, const char *font);
void ox_window_set_click(OxWindow *win, OxWindowClick click);
void ox_window_set_strut(OxWindow *win, int top, int bottom, int left, int right);
void ox_window_show(OxWindow *win);
void ox_window_hide(OxWindow *win);
void ox_window_move(OxWindow *win, int x, int y);
void ox_window_resize(OxWindow *win, int w, int h);
Window ox_window_handle(OxWindow *win);
```

### Draw

```c
void ox_draw_rect(OxWindow *win, int x, int y, int w, int h, const char *color);
void ox_draw_text(OxWindow *win, int x, int y, const char *text, const char *fg);
void ox_draw_xpm(OxWindow *win, int x, int y, const char *path);
void ox_draw_sep(OxWindow *win, int x, int y, const char *sep, const char *fg);
void ox_draw_flush(OxWindow *win);
int ox_text_width(OxWindow *win, const char *text);
```

### Loop

```c
void ox_init(void);
void ox_run(void);
void ox_quit(void);
```

## Creating Widgets

A widget is a data source with an update callback. It writes its output into a 256-byte buffer.

### Basic widget

```c
#include "ox.h"

static void my_update(void *ctx, char *buf, size_t len) {
    (void)ctx;
    snprintf(buf, len, "hello %d", 42);
}

int main(void) {
    ox_init();

    OxWindow *win = ox_window_new(0, 0, 200, 28);
    ox_window_set_bg(win, "#000000");
    ox_window_set_font(win, "monospace:size=11");
    ox_window_show(win);

    OxWidget *w = ox_widget_new("mywidget", 1.0);  /* update every 1s */
    ox_widget_set_update(w, my_update, NULL);

    /* render loop */
    while (1) {
        ox_widget_update(w);
        ox_draw_rect(win, 0, 0, 200, 28, "#000000");
        ox_draw_text(win, 8, 18, ox_widget_get_label(w), "#ffffff");
        /* ... handle events, sleep ... */
    }

    return 0;
}
```

### Widget with context

Pass state to your widget via `ctx`:

```c
typedef struct {
    int min;
    int max;
} Range;

static void temp_update(void *ctx, char *buf, size_t len) {
    Range *r = ctx;
    FILE *f = fopen("/sys/class/thermal/thermal_zone0/temp", "r");
    if (f) {
        int raw = 0;
        fscanf(f, "%d", &raw);
        fclose(f);
        int c = raw / 1000;
        snprintf(buf, len, "%d°C", c);
    }
}

Range temp_range = { .min = 0, .max = 100 };
OxWidget *temp = ox_widget_new("temp", 5.0);
ox_widget_set_update(temp, temp_update, &temp_range);
```

### Widget with click handler

```c
static void on_click(void *ctx) {
    system("pactl set-sink-mute @DEFAULT_SINK@ toggle");
}

OxWidget *vol = ox_widget_new("vol", 5.0);
ox_widget_set_update(vol, vol_update, NULL);
ox_widget_set_click(vol, on_click);
```

### Static text widget

For widgets that don't need periodic updates:

```c
OxWidget *label = ox_widget_new("label", 0);  /* interval=0 */
ox_widget_set_label_text(label, "static text");
```

## Multiple Widgets

Create multiple widgets and add them to a bar:

```c
Bar bar = { .height = 28, .padding = 8, .fg = "#ffffff", .bg = "#000000" };

/* time widget */
OxWidget *time = ox_widget_new("time", 1.0);
ox_widget_set_update(time, time_update, NULL);
bar_add(&bar, time);

/* cpu widget */
OxWidget *cpu = ox_widget_new("cpu", 2.0);
ox_widget_set_update(cpu, cpu_update, NULL);
bar_add(&bar, cpu);

/* mem widget */
OxWidget *mem = ox_widget_new("mem", 2.0);
ox_widget_set_update(mem, mem_update, NULL);
bar_add(&bar, mem);
```

### Multiple bars

Create separate bars at different positions:

```c
/* left bar */
Bar left = { .height = 28, .padding = 8, .fg = "#ffffff", .bg = "#000000" };
bar_add(&left, ox_widget_new("time", 1.0));

/* right bar */
Bar right = { .height = 28, .padding = 8, .fg = "#b3afc2", .bg = "#000000" };
bar_add(&right, ox_widget_new("vol", 5.0));
bar_add(&right, ox_widget_new("bat", 30.0));

/* create windows */
left.win = ox_window_new(0, 0, left_w, 28);
right.win = ox_window_new(sw - right_w, 0, right_w, 28);
```

### Rendering a bar

```c
static void bar_render(Bar *b) {
    int cy = b->height / 2 + ox_text_width(b->win, "X") / 3;
    int w = bar_width(b, b->win);
    ox_draw_rect(b->win, 0, 0, w, b->height, b->bg);
    int cx = b->padding;
    for (int i = 0; i < b->count; i++) {
        if (i > 0) {
            ox_draw_text(b->win, cx, cy, " | ", b->sep);
            cx += ox_text_width(b->win, " | ") + b->padding;
        }
        const char *label = ox_widget_get_label(b->widgets[i]);
        const char *color = ox_widget_get_fg(b->widgets[i]) ?: b->fg;
        ox_draw_text(b->win, cx, cy, label, color);
        cx += ox_text_width(b->win, label) + b->padding;
    }
}
```

### Update loop

```c
struct timespec ts = { .tv_sec = 0, .tv_nsec = 100000000 };  /* 100ms */
for (;;) {
    clock_gettime(CLOCK_MONOTONIC, &now);
    double cur = now.tv_sec + now.tv_nsec / 1e9;

    /* update widgets on interval */
    for (int i = 0; i < bar.count; i++) {
        OxWidget *w = bar.widgets[i];
        if (ox_widget_get_interval(w) > 0 &&
            cur - ox_widget_get_last_update(w) >= ox_widget_get_interval(w)) {
            ox_widget_update(w);
            ox_widget_set_last_update(w, cur);
        }
    }

    /* render */
    bar_render(&bar);

    /* handle events */
    Display *dpy = ox_display();
    while (XPending(dpy) > 0) {
        XEvent ev;
        XNextEvent(dpy, &ev);
        /* handle clicks ... */
    }

    XFlush(dpy);
    nanosleep(&ts, NULL);
}
```

## XPM Icons

```c
ox_draw_xpm(win, x, y, "path/to/icon.xpm");
```

## OSD (On-Screen Display)

```c
/* create hidden window */
OxWindow *osd = ox_window_new(x, y, 30, 200);
ox_window_set_bg(osd, "#111111");

/* show on signal */
signal(SIGUSR1, handler);

/* in signal handler */
if (show_osd) {
    ox_window_show(osd);
    /* auto-hide after timeout */
}
```

## EWMH Struts

Reserve screen space for your bar:

```c
ox_window_set_strut(win, 28, 0, 0, 0);  /* 28px at top */
```

## Signals

```c
SIGHUP   — quit (via ox_quit)
SIGUSR1  — custom (e.g., show volume OSD)
SIGUSR2  — custom (e.g., show brightness OSD)
SIGINT   — quit
SIGTERM  — quit
```

## License

MIT
