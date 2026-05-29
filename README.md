# ox

A minimal C toolkit for building X11 status bars and widgets.

No config files. Write C, not config.

## Quick Start

```c
#include "ox.h"

static void my_update(void *ctx, char *buf, size_t len) {
    (void)ctx;
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    snprintf(buf, len, "%02d:%02d:%02d", tm->tm_hour, tm->tm_min, tm->tm_sec);
}

static void render(OxWindow *win, void *ctx) {
    (void)ctx;
    OxWidget *w = ctx;
    ox_draw_rect(win, 0, 0, 200, 28, "#000000");
    ox_draw_text(win, 8, 18, ox_widget_get_label(w), "#ffffff");
}

int main(void) {
    ox_init();
    OxWindow *win = ox_window_new(0, 0, 200, 28);
    ox_window_set_bg(win, "#000000");
    ox_window_show(win);

    OxWidget *w = ox_widget_new("clock", 1.0);
    ox_widget_set_update(w, my_update, NULL);
    ox_window_set_render(win, render, w);

    ox_run();
    return 0;
}
```

## Build

```bash
make
```

Dependencies: libx11, libxft, libxpm, freetype2, fontconfig

## Architecture

ox uses an **event-driven rendering** model:

- Widgets update in background threads (non-blocking)
- Rendering only happens when widget data changes (`dirty` flag)
- X11 `ExposeEvent` triggers redraw
- No fixed 100ms timer — redraws on demand

```
┌─────────────────────────────────────────────┐
│                 Event Loop                   │
│  ┌─────────┐  ┌──────────┐  ┌────────────┐ │
│  │ Widgets │  │ X11      │  │ Rendering  │ │
│  │ (dirty) │→ │ Events   │→ │ (on change)│ │
│  └─────────┘  └──────────┘  └────────────┘ │
│       ↑              ↑              │       │
│  ┌─────────┐  ┌──────────┐  ┌──────┴─────┐ │
│  │ Threads │  │ Expose   │  │ Draw       │ │
│  │ (popen) │  │ Click    │  │ (Xft/XPM)  │ │
│  └─────────┘  └──────────┘  └────────────┘ │
└─────────────────────────────────────────────┘
```

## API Reference

### Core

#### ox_init

```c
void ox_init(void);
```

Initialize ox. Opens X11 display. Must be called first.

#### ox_run

```c
void ox_run(void);
```

Start event loop. Blocks until `ox_quit()` or signal.

#### ox_quit

```c
void ox_quit(void);
```

Stop event loop.

#### ox_display / ox_screen

```c
Display *ox_display(void);
int ox_screen(void);
```

Get X11 display and screen for direct Xlib calls.

---

### Widget

#### ox_widget_new

```c
OxWidget *ox_widget_new(const char *name, double interval);
```

Create widget. `interval` in seconds (0 = manual update).

#### ox_widget_destroy

```c
void ox_widget_destroy(OxWidget *w);
```

Free widget.

#### ox_widget_set_update

```c
void ox_widget_set_update(OxWidget *w, OxWidgetUpdate update, void *ctx);
```

Set update callback: `void (*)(void *ctx, char *buf, size_t len)`.

Callback writes output to `buf` (256 bytes). If output changes, widget is marked dirty.

#### ox_widget_set_click

```c
void ox_widget_set_click(OxWidget *w, OxWidgetClick click);
```

Set click callback: `void (*)(void *ctx)`.

#### ox_widget_set_icon

```c
void ox_widget_set_icon(OxWidget *w, const char *icon);
```

Set icon prefix. Rendered before label text.

#### ox_widget_set_label_text

```c
void ox_widget_set_label_text(OxWidget *w, const char *text);
```

Set static label (for interval=0 widgets).

#### ox_widget_set_colors

```c
void ox_widget_set_colors(OxWidget *w, const char *fg, const char *bg);
```

Override per-widget colors. `NULL` = use bar default.

#### ox_widget_set_render_progress

```c
void ox_widget_set_render_progress(OxWidget *w, int enable);
```

Enable progress bar rendering (label = percentage 0-100).

#### ox_widget_update

```c
void ox_widget_update(OxWidget *w);
```

Trigger update callback. Auto-called when interval elapses.

#### ox_widget_do_click

```c
void ox_widget_do_click(OxWidget *w);
```

Invoke click callback programmatically.

#### ox_widget_is_dirty / ox_widget_clear_dirty

```c
int ox_widget_is_dirty(OxWidget *w);
void ox_widget_clear_dirty(OxWidget *w);
```

Check/clear dirty flag. Widget is marked dirty when update produces new output.

#### Getters

```c
const char *ox_widget_get_name(OxWidget *w);
const char *ox_widget_get_label(OxWidget *w);
const char *ox_widget_get_icon(OxWidget *w);
const char *ox_widget_get_fg(OxWidget *w);
const char *ox_widget_get_bg(OxWidget *w);
double ox_widget_get_interval(OxWidget *w);
double ox_widget_get_last_update(OxWidget *w);
void ox_widget_set_last_update(OxWidget *w, double t);
```

---

### Window

#### ox_window_new

```c
OxWindow *ox_window_new(int x, int y, int w, int h);
```

Create X11 window. Sets override_redirect, `_NET_WM_WINDOW_TYPE_DOCK`, `ExposureMask`.

#### ox_window_destroy

```c
void ox_window_destroy(OxWindow *win);
```

Free window.

#### ox_window_set_bg

```c
void ox_window_set_bg(OxWindow *win, const char *color);
```

Set background color. Marks window dirty.

#### ox_window_set_font

```c
void ox_window_set_font(OxWindow *win, const char *font);
```

Set Xft font. Marks window dirty.

#### ox_window_set_render

```c
void ox_window_set_render(OxWindow *win, OxRenderFn fn, void *ctx);
```

Set render callback: `void (*)(OxWindow *win, void *ctx)`.

Called when window needs drawing (initial, expose, dirty widgets).

#### ox_window_set_click

```c
void ox_window_set_click(OxWindow *win, OxWindowClick click);
```

Set window click handler.

#### ox_window_set_strut

```c
void ox_window_set_strut(OxWindow *win, int top, int bottom, int left, int right);
```

Set EWMH struts. Reserves screen space.

#### ox_window_show / ox_window_hide

```c
void ox_window_show(OxWindow *win);
void ox_window_hide(OxWindow *win);
```

Map/unmap window.

#### ox_window_move / ox_window_resize

```c
void ox_window_move(OxWindow *win, int x, int y);
void ox_window_resize(OxWindow *win, int w, int h);
```

Move/resize window.

#### ox_window_handle

```c
Window ox_window_handle(OxWindow *win);
```

Get X11 Window handle for event matching.

---

### Draw

#### ox_draw_rect

```c
void ox_draw_rect(OxWindow *win, int x, int y, int w, int h, const char *color);
```

Draw filled rectangle.

#### ox_draw_text

```c
void ox_draw_text(OxWindow *win, int x, int y, const char *text, const char *fg);
```

Draw text with foreground color.

#### ox_draw_xpm

```c
void ox_draw_xpm(OxWindow *win, int x, int y, const char *path);
```

Draw XPM image from file.

#### ox_draw_sep

```c
void ox_draw_sep(OxWindow *win, int x, int y, const char *sep, const char *fg);
```

Draw separator (alias for `ox_draw_text`).

#### ox_draw_flush

```c
void ox_draw_flush(OxWindow *win);
```

Flush X11 drawing commands.

#### ox_text_width

```c
int ox_text_width(OxWindow *win, const char *text);
```

Measure text pixel width.

---

## Event Loop Pattern

```c
for (;;) {
    /* 1. Update widgets on interval */
    for (int i = 0; i < n; i++) {
        OxWidget *w = widgets[i];
        if (ox_widget_get_interval(w) > 0 &&
            cur - ox_widget_get_last_update(w) >= ox_widget_get_interval(w)) {
            ox_widget_update(w);  /* sets dirty if output changed */
            ox_widget_set_last_update(w, cur);
        }
    }

    /* 2. Redraw only if something changed */
    int any_dirty = 0;
    for (int i = 0; i < n; i++)
        if (ox_widget_is_dirty(widgets[i])) any_dirty = 1;

    if (any_dirty) {
        render(win, ctx);
        for (int i = 0; i < n; i++)
            ox_widget_clear_dirty(widgets[i]);
    }

    /* 3. Handle X11 events */
    while (XPending(dpy) > 0) {
        XEvent ev;
        XNextEvent(dpy, &ev);
        if (ev.type == Expose) render(win, ctx);  /* expose = redraw */
        if (ev.type == ButtonPress) { /* handle clicks */ }
    }

    XFlush(dpy);
    nanosleep(&ts, NULL);
}
```

## Threaded Commands

Shell commands run in background threads:

```c
OxWidget *w = ox_widget_new("vol", 5.0);
/* Widget runs: pactl get-sink-volume ... in a pthread */
/* Non-blocking, updates label when complete */
```

Output is compared to previous label. If different, widget is marked dirty.

## Signals

| Signal | Action |
|--------|--------|
| `SIGINT` | Quit |
| `SIGTERM` | Quit |
| `SIGHUP` | Quit |
| `SIGUSR1` | Custom |
| `SIGUSR2` | Custom |

## License

MIT
