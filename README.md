# ox

A minimal C toolkit for building X11 status bars and widgets


## [00] Quick Start

```c
#include "ox.h"

static void clock_update(void *ctx, char *buf, size_t len) {
    (void)ctx;
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    snprintf(buf, len, "%02d:%02d:%02d", tm->tm_hour, tm->tm_min, tm->tm_sec);
}

static void on_timeout(OxMain *m, double now) {
    OxWidget *w = m->ctx;
    if (now - ox_widget_get_last_update(w) >= ox_widget_get_interval(w)) {
        ox_widget_update(w);
        ox_widget_set_last_update(w, now);
    }
}

static void on_event(OxMain *m, XEvent *ev) {
    (void)ev;
    OxWidget *w = m->ctx;
    if (ev->type == Expose) {
        ox_draw_rect(win, 0, 0, 200, 28, "#000000");
        ox_draw_text(win, 8, 18, ox_widget_get_label(w), "#ffffff");
    }
}

int main(void) {
    ox_init();

    OxWindow *win = ox_window_new(0, 0, 200, 28);
    ox_window_set_bg(win, "#000000");
    ox_window_show(win);

    OxWidget *w = ox_widget_new("clock", 1.0);
    ox_widget_set_update(w, clock_update, NULL);

    OxMain loop = { .on_event = on_event, .on_timeout = on_timeout, .ctx = w };
    ox_main(&loop);
    ox_cleanup();
    return 0;
}
```


## [01] Build

```
make              # builds libox.a only
make examples     # builds all examples
make install      # installs libox.a, ox.h, libox.pc
```

Dependencies: libx11, libxft, libxpm, freetype2, fontconfig
Optional: libasound (for xmobar volume widget)


## [02] Architecture

ox has three layers:

```
Widget (state)         Window (X11 surface)      Draw (primitives)
--------------         --------------------       ----------------
OxWidget               OxWindow                   ox_draw_rect
- name                 - X11 window handle        ox_draw_text
- label (text)         - Xft font                 ox_draw_xpm
- update callback      - background color         ox_text_width
- click callback       - strut (dock reservation)
- async cmd (fork)     - show / hide
- dirty flag
```

Event loop:

```
ox_main()
----------
select() blocks on: X11 fd, IPC fd, widget pipe fds
     |
     +-- on_timeout(now)  ->  update widgets, check dirty, redraw
     +-- on_event(ev)     ->  handle Expose, ButtonPress, KeyPress
     +-- on_ipc(msg)      ->  handle external commands
```

Widget update flow:

```
ox_widget_update(w)
     |
     +-- has cmd?    -> fork()+exec(), read stdout non-blocking
     |                    (pipe fd exposed for select() monitoring)
     |
     +-- has callback?  -> call update(ctx, buf, 256)
                             compare buf to current label
                             if different -> dirty = 1
```


## [03] API Reference

References link to source on GitHub (oxbar repo).

### Core

- `ox_init()` [src/draw.c:88](https://github.com/fahmiirsyadk/oxbar/blob/main/src/draw.c#L88)
- `ox_cleanup()` [src/draw.c:93](https://github.com/fahmiirsyadk/oxbar/blob/main/src/draw.c#L93)
- `ox_main(OxMain*)` [src/loop.c:77](https://github.com/fahmiirsyadk/oxbar/blob/main/src/loop.c#L77)
- `ox_quit(OxMain*)` [src/loop.c:73](https://github.com/fahmiirsyadk/oxbar/blob/main/src/loop.c#L73)
- `ox_display()` [src/draw.c:85](https://github.com/fahmiirsyadk/oxbar/blob/main/src/draw.c#L85)
- `ox_screen()` [src/draw.c:86](https://github.com/fahmiirsyadk/oxbar/blob/main/src/draw.c#L86)

### Widget

- `ox_widget_new()` [src/widget.c:97](https://github.com/fahmiirsyadk/oxbar/blob/main/src/widget.c#L97)
- `ox_widget_destroy()` [src/widget.c:106](https://github.com/fahmiirsyadk/oxbar/blob/main/src/widget.c#L106)
- `ox_widget_set_update()` [src/widget.c:121](https://github.com/fahmiirsyadk/oxbar/blob/main/src/widget.c#L121)
- `ox_widget_set_click()` [src/widget.c:126](https://github.com/fahmiirsyadk/oxbar/blob/main/src/widget.c#L126)
- `ox_widget_set_icon()` [src/widget.c:130](https://github.com/fahmiirsyadk/oxbar/blob/main/src/widget.c#L130)
- `ox_widget_set_cmd()` [src/widget.c:135](https://github.com/fahmiirsyadk/oxbar/blob/main/src/widget.c#L135)
- `ox_widget_set_label_text()` [src/widget.c:140](https://github.com/fahmiirsyadk/oxbar/blob/main/src/widget.c#L140)
- `ox_widget_set_colors()` [src/widget.c:149](https://github.com/fahmiirsyadk/oxbar/blob/main/src/widget.c#L149)
- `ox_widget_update()` [src/widget.c:251](https://github.com/fahmiirsyadk/oxbar/blob/main/src/widget.c#L251)
- `ox_widget_do_click()` [src/widget.c:266](https://github.com/fahmiirsyadk/oxbar/blob/main/src/widget.c#L266)
- `ox_widget_get_fd()` [src/widget.c:197](https://github.com/fahmiirsyadk/oxbar/blob/main/src/widget.c#L197)
- `ox_widget_read()` [src/widget.c:214](https://github.com/fahmiirsyadk/oxbar/blob/main/src/widget.c#L214)
- `ox_widget_get_label()` [src/widget.c:270](https://github.com/fahmiirsyadk/oxbar/blob/main/src/widget.c#L270)
- `ox_widget_get_icon()` [src/widget.c:271](https://github.com/fahmiirsyadk/oxbar/blob/main/src/widget.c#L271)
- `ox_widget_get_fg()` [src/widget.c:272](https://github.com/fahmiirsyadk/oxbar/blob/main/src/widget.c#L272)
- `ox_widget_get_bg()` [src/widget.c:273](https://github.com/fahmiirsyadk/oxbar/blob/main/src/widget.c#L273)
- `ox_widget_get_interval()` [src/widget.c:274](https://github.com/fahmiirsyadk/oxbar/blob/main/src/widget.c#L274)
- `ox_widget_get_last_update()` [src/widget.c:275](https://github.com/fahmiirsyadk/oxbar/blob/main/src/widget.c#L275)
- `ox_widget_set_last_update()` [src/widget.c:276](https://github.com/fahmiirsyadk/oxbar/blob/main/src/widget.c#L276)
- `ox_widget_is_dirty()` [src/widget.c:277](https://github.com/fahmiirsyadk/oxbar/blob/main/src/widget.c#L277)
- `ox_widget_clear_dirty()` [src/widget.c:278](https://github.com/fahmiirsyadk/oxbar/blob/main/src/widget.c#L278)

### Window

- `ox_window_new()` [src/draw.c:121](https://github.com/fahmiirsyadk/oxbar/blob/main/src/draw.c#L121)
- `ox_window_new_floating()` [src/draw.c:157](https://github.com/fahmiirsyadk/oxbar/blob/main/src/draw.c#L157)
- `ox_window_destroy()` [src/draw.c:168](https://github.com/fahmiirsyadk/oxbar/blob/main/src/draw.c#L168)
- `ox_window_set_bg()` [src/draw.c:179](https://github.com/fahmiirsyadk/oxbar/blob/main/src/draw.c#L179)
- `ox_window_set_font()` [src/draw.c:184](https://github.com/fahmiirsyadk/oxbar/blob/main/src/draw.c#L184)
- `ox_window_set_strut()` [src/draw.c:189](https://github.com/fahmiirsyadk/oxbar/blob/main/src/draw.c#L189)
- `ox_window_set_wm_class()` [src/draw.c:204](https://github.com/fahmiirsyadk/oxbar/blob/main/src/draw.c#L204)
- `ox_window_show()` [src/draw.c:212](https://github.com/fahmiirsyadk/oxbar/blob/main/src/draw.c#L212)
- `ox_window_hide()` [src/draw.c:218](https://github.com/fahmiirsyadk/oxbar/blob/main/src/draw.c#L218)
- `ox_window_handle()` [src/draw.c:224](https://github.com/fahmiirsyadk/oxbar/blob/main/src/draw.c#L224)

### Draw

- `ox_draw_rect()` [src/draw.c:234](https://github.com/fahmiirsyadk/oxbar/blob/main/src/draw.c#L234)
- `ox_draw_text()` [src/draw.c:273](https://github.com/fahmiirsyadk/oxbar/blob/main/src/draw.c#L273)
- `ox_draw_xpm()` [src/draw.c:301](https://github.com/fahmiirsyadk/oxbar/blob/main/src/draw.c#L301)
- `ox_text_width()` [src/draw.c:284](https://github.com/fahmiirsyadk/oxbar/blob/main/src/draw.c#L284)

### IPC

- `ox_ipc_init()` [src/ipc.c:63](https://github.com/fahmiirsyadk/oxbar/blob/main/src/ipc.c#L63)
- `ox_ipc_fd()` [src/ipc.c:84](https://github.com/fahmiirsyadk/oxbar/blob/main/src/ipc.c#L84)
- `ox_ipc_send()` [src/ipc.c:88](https://github.com/fahmiirsyadk/oxbar/blob/main/src/ipc.c#L88)
- `ox_ipc_recv()` [src/ipc.c:111](https://github.com/fahmiirsyadk/oxbar/blob/main/src/ipc.c#L111)
- `ox_ipc_cleanup()` [src/ipc.c:138](https://github.com/fahmiirsyadk/oxbar/blob/main/src/ipc.c#L138)

### Types

- `OxMain` [include/ox.h:17](https://github.com/fahmiirsyadk/oxbar/blob/main/include/ox.h#L17)
- `OxWidgetUpdate` [include/ox.h:11](https://github.com/fahmiirsyadk/oxbar/blob/main/include/ox.h#L11)
- `OxWidgetClick` [include/ox.h:12](https://github.com/fahmiirsyadk/oxbar/blob/main/include/ox.h#L12)
- `OxEventFn` [include/ox.h:13](https://github.com/fahmiirsyadk/oxbar/blob/main/include/ox.h#L13)
- `OxTimeoutFn` [include/ox.h:14](https://github.com/fahmiirsyadk/oxbar/blob/main/include/ox.h#L14)
- `OxIpcFn` [include/ox.h:15](https://github.com/fahmiirsyadk/oxbar/blob/main/include/ox.h#L15)


## [04] Widget Guide

Creating a widget:

```c
OxWidget *w = ox_widget_new("name", interval_seconds);
```

Setting update logic — two approaches:

A) Callback (for reading /proc, sysfs, time, etc.):

```c
static void my_update(void *ctx, char *buf, size_t len) {
    (void)ctx;
    FILE *f = fopen("/proc/loadavg", "r");
    if (!f) { snprintf(buf, len, "??"); return; }
    float load;
    fscanf(f, "%f", &load);
    fclose(f);
    snprintf(buf, len, "%.1f", load);
}

OxWidget *w = ox_widget_new("load", 2.0);
ox_widget_set_update(w, my_update, NULL);
```

B) Shell command (for pactl, amixer, etc.):

```c
OxWidget *w = ox_widget_new("vol", 5.0);
ox_widget_set_cmd(w, "amixer get Master | awk -F'[][]' '/%/ {print $2}'");
```

The command runs asynchronously (fork+exec, non-blocking pipe).
When complete, the first line of stdout becomes the widget label.

Setting click behavior:

```c
static void on_click(void *ctx) {
    system("wpctl set-mute @DEFAULT_SINK@ toggle");
}
ox_widget_set_click(w, on_click);
```

Per-widget colors:

```c
ox_widget_set_colors(w, "#ff0000", NULL);  /* red fg, default bg */
```

Static label (no update callback):

```c
OxWidget *w = ox_widget_new("power", 0);
ox_widget_set_label_text(w, "off");
```


## [05] Multiple Widgets / Bars

Bars are arrays of widgets. Each widget updates independently on its own
interval. See `examples/simple-bar.c` for a complete implementation.

Bar struct pattern:

```c
typedef struct {
    OxWindow *win;
    OxWidget *widgets[16];
    int count;
    int height, padding;
    const char *fg, *bg;
} Bar;

static void bar_add(Bar *b, OxWidget *w) {
    b->widgets[b->count++] = w;
}
```

Render draws all widgets in a row:

```c
static void bar_render(Bar *b) {
    int cx = b->padding;
    int cy = b->height / 2;
    for (int i = 0; i < b->count; i++) {
        if (i > 0) {
            ox_draw_text(b->win, cx, cy, " | ", "#555555");
            cx += ox_text_width(b->win, " | ") + b->padding;
        }
        ox_draw_text(b->win, cx, cy,
            ox_widget_get_label(b->widgets[i]), b->fg);
        cx += ox_text_width(b->win,
            ox_widget_get_label(b->widgets[i])) + b->padding;
    }
}
```

Timeout updates widgets, redraws only if dirty:

```c
static void on_timeout(OxMain *m, double now) {
    Bar *b = m->ctx;
    int any_dirty = 0;
    for (int i = 0; i < b->count; i++) {
        OxWidget *w = b->widgets[i];
        if (ox_widget_get_interval(w) > 0 &&
            now - ox_widget_get_last_update(w) >= ox_widget_get_interval(w)) {
            ox_widget_update(w);
            ox_widget_set_last_update(w, now);
        }
        if (ox_widget_is_dirty(w)) any_dirty = 1;
    }
    if (any_dirty) {
        bar_render(b);
        for (int i = 0; i < b->count; i++)
            ox_widget_clear_dirty(b->widgets[i]);
    }
}
```

Multiple bars (left / center / right):

```c
Bar left   = { .height=28, .padding=8, .fg="#fff", .bg="#000" };
Bar center = { .height=28, .padding=8, .fg="#fff", .bg="#000" };
Bar right  = { .height=28, .padding=8, .fg="#fff", .bg="#000" };

left.win   = ox_window_new(0,        0, left_w,   h);
center.win = ox_window_new((sw-cw)/2, 0, center_w, h);
right.win  = ox_window_new(sw-rw,     0, right_w,  h);
```


## [06] Click Handling

See `examples/xmobar.c` for a complete implementation with hit-testing.

The WidgetLayout struct stores bounding boxes computed during render:

```c
typedef struct { int x, width; } WidgetLayout;
```

In your render function, store layout per widget:

```c
b->layout[i].x = cx;
b->layout[i].width = ww;
```

In `on_event`, use `bar_hit_test` to find which widget was clicked:

```c
static int bar_hit_test(Bar *b, int x) {
    for (int i = 0; i < b->count; i++)
        if (x >= b->layout[i].x &&
            x < b->layout[i].x + b->layout[i].width)
            return i;
    return -1;
}

static void on_event(OxMain *m, XEvent *ev) {
    Bar *b = m->ctx;
    if (ev->type == ButtonPress) {
        if (ev->xbutton.window == ox_window_handle(b->win)) {
            int idx = bar_hit_test(b, ev->xbutton.x);
            if (idx >= 0) ox_widget_do_click(b->widgets[idx]);
        }
    }
}
```


## [07] IPC

IPC lets external scripts send commands to a running ox bar.
See `src/ipc.c` for the implementation.

Socket path: `$XDG_RUNTIME_DIR/ox-ipc-<pid>.sock`

Send a command from a script:

```sh
./xmobar &
SOCK="/run/user/$(id -u)/ox-ipc-$(pgrep xmobar).sock"
echo "update-vol" | socat - UNIX-CONNECT:"path=$SOCK"
```

Handle commands in the bar:

```c
static void on_ipc(OxMain *m, const char *msg) {
    if (strcmp(msg, "update-vol") == 0) {
        ox_widget_update(w_vol);
        /* redraw happens automatically if dirty */
    }
}

/* in main: */
ox_ipc_init();
OxMain loop = {
    .on_ipc = on_ipc,
    .ipc_fd = ox_ipc_fd(),
};
```


## [08] Extending

Adding a new widget type:

1. Write an update callback:

```c
static void net_update(void *ctx, char *buf, size_t len) {
    FILE *f = fopen("/proc/net/dev", "r");
    if (!f) { snprintf(buf, len, "??"); return; }
    /* parse and format */
    fclose(f);
}
```

2. Create and configure:

```c
OxWidget *w = ox_widget_new("net", 3.0);
ox_widget_set_update(w, net_update, NULL);
ox_widget_set_colors(w, "#00ff00", NULL);
```

3. Add to your bar:

```c
bar_add(&right, w);
```

The event loop handles timing, dirty tracking, and rendering.

Adding a draw primitive — keep it in `src/draw.c`:

```c
void ox_draw_foo(OxWindow *win, int x, int y /* params */) {
    /* use win->dpy, win->gc, win->draw for X11 operations */
}
```

Add the declaration to `include/ox.h` under the Draw section.


## [09] Examples

| Example | Description |
|---------|-------------|
| simple-bar | full-width bar: time, cpu, mem |
| multi-bar | three bars: left/center/right panels |
| vertical-bar | vertical sidebar with stacked widgets |
| osd | on-screen display (volume, brightness) |
| xmobar | full status bar: ALSA vol, battery, clock, workspaces |
| search | fuzzy search launcher (dmenu-like) |

Source: `examples/*.c`


## [10] Signals

| Signal | Action |
|--------|--------|
| SIGINT | exit event loop cleanly |
| SIGTERM | exit event loop cleanly |
| SIGCHLD | ignored (child processes auto-reap) |


## [11] License

MIT
