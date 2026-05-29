#ifndef OX_H
#define OX_H

#include <stddef.h>
#include <X11/Xlib.h>

typedef struct OxWidget OxWidget;
typedef struct OxWindow OxWindow;

typedef void (*OxWidgetUpdate)(void *ctx, char *buf, size_t len);
typedef void (*OxWidgetClick)(void *ctx);
typedef void (*OxWindowClick)(OxWindow *win, int x, int y, int button);
typedef void (*OxRenderFn)(OxWindow *win, void *ctx);

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
int ox_widget_is_dirty(OxWidget *w);
void ox_widget_clear_dirty(OxWidget *w);

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
void ox_window_set_render(OxWindow *win, OxRenderFn fn, void *ctx);

void ox_draw_rect(OxWindow *win, int x, int y, int w, int h, const char *color);
void ox_draw_text(OxWindow *win, int x, int y, const char *text, const char *fg);
void ox_draw_xpm(OxWindow *win, int x, int y, const char *path);
void ox_draw_sep(OxWindow *win, int x, int y, const char *sep, const char *fg);
void ox_draw_flush(OxWindow *win);
int ox_text_width(OxWindow *win, const char *text);

Display *ox_display(void);
int ox_screen(void);
void ox_init(void);
void ox_run(void);
void ox_quit(void);

#endif
