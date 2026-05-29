#ifndef OX_H
#define OX_H

#include <stddef.h>
#include <X11/Xlib.h>

typedef struct OxWidget OxWidget;
typedef struct OxWindow OxWindow;
typedef struct OxMain OxMain;

typedef void (*OxWidgetUpdate)(void *ctx, char *buf, size_t len);
typedef void (*OxWidgetClick)(void *ctx);
typedef void (*OxEventFn)(OxMain *m, XEvent *ev);
typedef void (*OxTimeoutFn)(OxMain *m, double now);
typedef void (*OxIpcFn)(OxMain *m, const char *msg);

struct OxMain {
    OxEventFn on_event;
    OxTimeoutFn on_timeout;
    OxIpcFn on_ipc;
    void *ctx;
    int ipc_fd;
    int extra_fds[8];
    int extra_fd_count;
    int timeout_ms;
    volatile int running;
};

/* Widget */
OxWidget *ox_widget_new(const char *name, double interval);
void ox_widget_destroy(OxWidget *w);
void ox_widget_set_update(OxWidget *w, OxWidgetUpdate update, void *ctx);
void ox_widget_set_click(OxWidget *w, OxWidgetClick click);
void ox_widget_set_icon(OxWidget *w, const char *icon);
void ox_widget_set_cmd(OxWidget *w, const char *cmd);
void ox_widget_set_label_text(OxWidget *w, const char *text);
void ox_widget_set_colors(OxWidget *w, const char *fg, const char *bg);
void ox_widget_update(OxWidget *w);
void ox_widget_do_click(OxWidget *w);
int ox_widget_get_fd(OxWidget *w);
void ox_widget_read(OxWidget *w);
const char *ox_widget_get_label(OxWidget *w);
const char *ox_widget_get_icon(OxWidget *w);
const char *ox_widget_get_fg(OxWidget *w);
const char *ox_widget_get_bg(OxWidget *w);
double ox_widget_get_interval(OxWidget *w);
double ox_widget_get_last_update(OxWidget *w);
void ox_widget_set_last_update(OxWidget *w, double t);
int ox_widget_is_dirty(OxWidget *w);
void ox_widget_clear_dirty(OxWidget *w);

/* Window */
OxWindow *ox_window_new(int x, int y, int w, int h);
OxWindow *ox_window_new_floating(int x, int y, int w, int h);
void ox_window_destroy(OxWindow *win);
void ox_window_set_bg(OxWindow *win, const char *color);
void ox_window_set_font(OxWindow *win, const char *font);
void ox_window_set_strut(OxWindow *win, int top, int bottom, int left, int right);
void ox_window_set_wm_class(OxWindow *win, const char *wm_class, const char *wm_name);
void ox_window_show(OxWindow *win);
void ox_window_hide(OxWindow *win);
Window ox_window_handle(OxWindow *win);

/* Draw */
void ox_draw_rect(OxWindow *win, int x, int y, int w, int h, const char *color);
void ox_draw_text(OxWindow *win, int x, int y, const char *text, const char *fg);
void ox_draw_xpm(OxWindow *win, int x, int y, const char *path);
int ox_text_width(OxWindow *win, const char *text);

/* IPC */
int ox_ipc_init(void);
int ox_ipc_fd(void);
int ox_ipc_send(const char *msg);
int ox_ipc_recv(char *buf, size_t len);
void ox_ipc_cleanup(void);

/* Core */
Display *ox_display(void);
int ox_screen(void);
void ox_init(void);
void ox_main(OxMain *m);
void ox_quit(OxMain *m);
void ox_cleanup(void);

#endif
