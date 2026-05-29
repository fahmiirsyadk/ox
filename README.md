# ox
________________________________________________________________________________

A minimal C toolkit for building X11 status bars and widgets.

No config files. Write C, not config.


[00] Quick Start
________________________________________________________________________________

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
    OxWindow *win = ox_window_handle(/* your window */);
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


[01] Build
________________________________________________________________________________

```bash
make              # builds libox.a only
make examples     # builds all examples
make install      # installs libox.a, ox.h, libox.pc
```

Dependencies: libx11, libxft, libxpm, freetype2, fontconfig
Optional: libasound (for xmobar volume widget)


[02] Architecture
________________________________________________________________________________

ox has three layers:

```
  Widget (state)          Window (X11 surface)       Draw (primitives)
  ─────────────           ────────────────────        ────────────────
  OxWidget                OxWindow                    ox_draw_rect
  - name                  - X11 window handle         ox_draw_text
  - label (text)          - Xft font                  ox_draw_xpm
  - update callback       - background color          ox_text_width
  - click callback        - strut (dock reservation)
  - async cmd (fork/exec) - show/hide
  - dirty flag
```

Event loop:

```
  ox_main()
  ─────────
  select() blocks on: X11 fd, IPC fd, widget pipe fds
       │
       ├── on_timeout(now)  →  update widgets, check dirty, redraw
       ├── on_event(ev)     →  handle Expose, ButtonPress, KeyPress
       └── on_ipc(msg)      →  handle external commands
```

Widget update flow:

```
  ox_widget_update(w)
       │
       ├── has cmd?  → fork()+exec(), read stdout non-blocking
       │                (pipe fd exposed for select() monitoring)
       │
       └── has callback? → call update(ctx, buf, 256)
                            compare buf to current label
                            if different → dirty = 1
```


[03] Widget Guide
________________________________________________________________________________

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


[04] Multiple Widgets
________________________________________________________________________________

Bars are arrays of widgets. Each widget updates independently on its own interval.

```c
/* bar state */
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

/* render draws all widgets in a row */
static void bar_render(Bar *b) {
    int cx = b->padding;
    int cy = b->height / 2;
    for (int i = 0; i < b->count; i++) {
        if (i > 0) {
            ox_draw_text(b->win, cx, cy, " | ", "#555555");
            cx += ox_text_width(b->win, " | ") + b->padding;
        }
        ox_draw_text(b->win, cx, cy, ox_widget_get_label(b->widgets[i]), b->fg);
        cx += ox_text_width(b->win, ox_widget_get_label(b->widgets[i])) + b->padding;
    }
}

/* timeout updates all widgets, redraws if any changed */
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

Multiple bars (left/center/right):

```c
Bar left   = { .height = 28, .padding = 8, .fg = "#ffffff", .bg = "#000000" };
Bar center = { .height = 28, .padding = 8, .fg = "#ffffff", .bg = "#000000" };
Bar right  = { .height = 28, .padding = 8, .fg = "#ffffff", .bg = "#000000" };

/* create windows at different positions */
left.win   = ox_window_new(0, 0, left_w, h);
center.win = ox_window_new((sw - center_w) / 2, 0, center_w, h);
right.win  = ox_window_new(sw - right_w, 0, right_w, h);
```

Click handling per bar:

```c
static void on_event(OxMain *m, XEvent *ev) {
    Bar *b = m->ctx;
    if (ev->type == ButtonPress) {
        if (ev->xbutton.window == ox_window_handle(b->win)) {
            int cx = b->padding;
            for (int i = 0; i < b->count; i++) {
                int ww = ox_text_width(b->win, ox_widget_get_label(b->widgets[i])) + b->padding;
                if (ev->xbutton.x >= cx && ev->xbutton.x < cx + ww) {
                    ox_widget_do_click(b->widgets[i]);
                    break;
                }
                cx += ww;
            }
        }
    }
}
```


[05] IPC
________________________________________________________________________________

Send commands to a running ox bar from scripts:

```bash
# start the bar
./xmobar &

# send a command (from i3 config, keybind, etc.)
echo "update-vol" | socat - UNIX-CONNECT:path=/run/user/1000/ox-ipc-12345.sock
```

In the bar, handle IPC messages:

```c
static void on_ipc(OxMain *m, const char *msg) {
    if (strcmp(msg, "update-vol") == 0) {
        ox_widget_update(w_vol);
        /* redraw will happen automatically if dirty */
    }
}

/* in main: */
ox_ipc_init();
OxMain loop = {
    .on_ipc = on_ipc,
    .ipc_fd = ox_ipc_fd(),
    /* ... */
};
```


[06] Code Style
________________________________________________________________________________

Formatting:

- 4-space indentation (no tabs)
- Opening brace on same line for functions and control flow
- Single blank line between functions
- No trailing whitespace

Naming:

- ox_ prefix for all public API functions
- snake_case for functions and variables
- UPPER_CASE for #define constants
- OxPascalCase for types (OxWidget, OxWindow, OxMain)
- g_ prefix for file-scope globals (g_dpy, g_screen)

Callbacks:

- Always check for NULL before invoking
- Pass user context as first parameter
- Return type void for all callbacks

Memory:

- strdup() for owned strings, free() in destroy functions
- calloc() for struct allocation
- No hidden allocations in draw functions

Error handling:

- Return NULL or -1 on failure (no exceptions)
- Log to stderr for critical errors (perror, fprintf)
- Graceful degradation (show "??%" instead of crashing)

Headers:

- _POSIX_C_SOURCE 200809L for getline, etc.
- _GNU_SOURCE for strcasestr (search example only)
- Include ox.h last among local headers


[07] Extending
________________________________________________________________________________

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

That's it. The event loop handles timing, dirty tracking, and rendering.

Adding a new draw primitive:

Keep it in draw.c. Follow the pattern:

```c
void ox_draw_foo(OxWindow *win, int x, int y, /* params */) {
    /* use win->dpy, win->gc, win->draw for X11 operations */
}
```

Add the declaration to ox.h under the Draw section.

Adding a new event type:

Add a callback field to OxMain, wire it in ox_main()'s select() loop,
and expose it via the API.


[08] Examples
________________________________________________________________________________

| Example       | Description                           |
|---------------|---------------------------------------|
| simple-bar    | Full-width bar: time, cpu, mem        |
| multi-bar     | Three bars: left/center/right panels  |
| vertical-bar  | Vertical sidebar with stacked widgets |
| osd           | On-screen display (volume/brightness) |
| xmobar        | Full status bar with ALSA, battery, clock |
| search        | Fuzzy search launcher (dmenu-like)    |


[09] Signals
________________________________________________________________________________

| Signal  | Action                        |
|---------|-------------------------------|
| SIGINT  | Exit event loop cleanly       |
| SIGTERM | Exit event loop cleanly       |
| SIGCHLD | Ignored (child auto-reap)     |


[10] License
________________________________________________________________________________

MIT
