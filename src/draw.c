#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xft/Xft.h>
#include <X11/xpm.h>
#include "ox.h"

#define MAX_FONTS 8

struct OxWindow {
    Display *dpy;
    int screen;
    Window win;
    GC gc;
    XftDraw *draw;
    XftFont *fonts[MAX_FONTS];
    int font_count;
    int text_offset;
    int width, height;
    int x, y;
    char *bg;
    int alpha;
    int mapped;
    int floating;
    OxWindowClick on_click;
    OxRenderFn on_render;
    void *render_ctx;
    int dirty;
};

static Display *g_dpy;
static int g_screen;

Display *ox_display(void) { return g_dpy; }
int ox_screen(void) { return g_screen; }

void ox_init(void) {
    g_dpy = XOpenDisplay(NULL);
    g_screen = DefaultScreen(g_dpy);
}

static unsigned long parse_hex(const char *hex) {
    if (!hex || hex[0] != '#') return 0;
    unsigned long r = 0, g = 0, b = 0;
    sscanf(hex + 1, "%02lx%02lx%02lx", &r, &g, &b);
    return (r << 16) | (g << 8) | b;
}

static void set_alpha_prop(OxWindow *win) {
    unsigned long opacity = (unsigned long)((win->alpha * 0xffffffffUL) / 255);
    Atom atom = XInternAtom(win->dpy, "_NET_WM_WINDOW_OPACITY", False);
    XChangeProperty(win->dpy, win->win, atom, XA_CARDINAL, 32,
        PropModeReplace, (unsigned char *)&opacity, 1);
}

OxWindow *ox_window_new(int x, int y, int w, int h) {
    OxWindow *win = calloc(1, sizeof(OxWindow));
    win->dpy = g_dpy;
    win->screen = g_screen;
    win->width = w;
    win->height = h;
    win->x = x;
    win->y = y;
    win->alpha = 255;
    win->mapped = 1;
    win->bg = strdup("#000000");
    win->fonts[0] = XftFontOpenName(g_dpy, g_screen, "monospace:size=11");
    win->font_count = 1;
    win->gc = XCreateGC(g_dpy, RootWindow(g_dpy, g_screen), 0, NULL);

    win->win = XCreateSimpleWindow(g_dpy, RootWindow(g_dpy, g_screen),
        x, y, w, h, 0, 0, parse_hex(win->bg));

    win->draw = XftDrawCreate(g_dpy, win->win,
        DefaultVisual(g_dpy, g_screen), DefaultColormap(g_dpy, g_screen));

    XSetWindowAttributes sa;
    sa.override_redirect = True;
    XChangeWindowAttributes(g_dpy, win->win, CWOverrideRedirect, &sa);

    Atom type = XInternAtom(g_dpy, "_NET_WM_WINDOW_TYPE_DOCK", False);
    XChangeProperty(g_dpy, win->win,
        XInternAtom(g_dpy, "_NET_WM_WINDOW_TYPE", False),
        XA_ATOM, 32, PropModeReplace, (unsigned char *)&type, 1);

    XSelectInput(g_dpy, win->win, ButtonPressMask | ExposureMask);
    return win;
}

OxWindow *ox_window_new_floating(int x, int y, int w, int h) {
    OxWindow *win = ox_window_new(x, y, w, h);
    win->floating = 1;
    Atom type = XInternAtom(g_dpy, "_NET_WM_WINDOW_TYPE_DIALOG", False);
    XChangeProperty(g_dpy, win->win,
        XInternAtom(g_dpy, "_NET_WM_WINDOW_TYPE", False),
        XA_ATOM, 32, PropModeReplace, (unsigned char *)&type, 1);
    return win;
}

void ox_window_destroy(OxWindow *win) {
    if (!win) return;
    XftDrawDestroy(win->draw);
    for (int i = 0; i < win->font_count; i++)
        XftFontClose(win->dpy, win->fonts[i]);
    XFreeGC(win->dpy, win->gc);
    XDestroyWindow(win->dpy, win->win);
    free(win->bg);
    free(win);
}

void ox_window_set_bg(OxWindow *win, const char *color) {
    free(win->bg);
    win->bg = strdup(color);
    win->dirty = 1;
}

void ox_window_set_font(OxWindow *win, const char *font) {
    XftFontClose(win->dpy, win->fonts[0]);
    win->fonts[0] = XftFontOpenName(win->dpy, win->screen, font);
    win->dirty = 1;
}

void ox_window_set_font_n(OxWindow *win, int idx, const char *font) {
    if (idx < 0 || idx >= MAX_FONTS) return;
    if (idx >= win->font_count) {
        for (int i = win->font_count; i <= idx; i++)
            win->fonts[i] = win->fonts[0];
        win->font_count = idx + 1;
    }
    XftFontClose(win->dpy, win->fonts[idx]);
    win->fonts[idx] = XftFontOpenName(win->dpy, win->screen, font);
    win->dirty = 1;
}

void ox_window_set_click(OxWindow *win, OxWindowClick click) {
    win->on_click = click;
}

void ox_window_set_render(OxWindow *win, OxRenderFn fn, void *ctx) {
    win->on_render = fn;
    win->render_ctx = ctx;
    win->dirty = 1;
}

void ox_window_set_text_offset(OxWindow *win, int offset) {
    win->text_offset = offset;
    win->dirty = 1;
}

void ox_window_set_alpha(OxWindow *win, int alpha) {
    win->alpha = alpha;
    set_alpha_prop(win);
}

void ox_window_set_wm_class(OxWindow *win, const char *wm_class, const char *wm_name) {
    XClassHint hint;
    hint.res_name = (char *)wm_name;
    hint.res_class = (char *)wm_class;
    XSetClassHint(win->dpy, win->win, &hint);
    XStoreName(win->dpy, win->win, wm_name);
}

void ox_window_set_strut(OxWindow *win, int top, int bottom, int left, int right) {
    unsigned long s[12] = {0};
    s[0] = left; s[1] = right; s[2] = top; s[3] = bottom;
    s[8] = win->x; s[9] = win->x + win->width - 1;
    XChangeProperty(win->dpy, win->win,
        XInternAtom(win->dpy, "_NET_WM_STRUT_PARTIAL", False),
        XA_CARDINAL, 32, PropModeReplace, (unsigned char *)s, 12);
}

void ox_window_show(OxWindow *win) {
    XMapWindow(win->dpy, win->win);
    XSync(win->dpy, False);
    win->mapped = 1;
    win->dirty = 1;
}

void ox_window_hide(OxWindow *win) {
    XUnmapWindow(win->dpy, win->win);
    XSync(win->dpy, False);
    win->mapped = 0;
}

void ox_window_toggle(OxWindow *win) {
    if (win->mapped) ox_window_hide(win);
    else ox_window_show(win);
}

void ox_window_move(OxWindow *win, int x, int y) {
    win->x = x; win->y = y;
    XMoveWindow(win->dpy, win->win, x, y);
}

void ox_window_resize(OxWindow *win, int w, int h) {
    win->width = w; win->height = h;
    XResizeWindow(win->dpy, win->win, w, h);
    win->dirty = 1;
}

void ox_draw_rect(OxWindow *win, int x, int y, int w, int h, const char *color) {
    XSetForeground(win->dpy, win->gc, parse_hex(color));
    XFillRectangle(win->dpy, win->win, win->gc, x, y, w, h);
}

void ox_draw_text(OxWindow *win, int x, int y, const char *text, const char *fg) {
    ox_draw_text_n(win, x, y, 0, text, fg);
}

void ox_draw_text_n(OxWindow *win, int x, int y, int font_idx, const char *text, const char *fg) {
    if (!text || !*text || font_idx < 0 || font_idx >= win->font_count) return;
    XRenderColor rc = { 0 };
    if (fg && fg[0] == '#') {
        unsigned int r = 0, g = 0, b = 0;
        sscanf(fg + 1, "%02x%02x%02x", &r, &g, &b);
        rc.red = r * 0x0101; rc.green = g * 0x0101; rc.blue = b * 0x0101;
        rc.alpha = 0xffff;
    }
    XftColor color;
    XftColorAllocValue(win->dpy, DefaultVisual(win->dpy, win->screen),
        DefaultColormap(win->dpy, win->screen), &rc, &color);
    int draw_y = y + win->text_offset;
    XftDrawStringUtf8(win->draw, &color, win->fonts[font_idx], x, draw_y,
        (const FcChar8 *)text, strlen(text));
    XftColorFree(win->dpy, DefaultVisual(win->dpy, win->screen),
        DefaultColormap(win->dpy, win->screen), &color);
}

int ox_text_width(OxWindow *win, const char *text) {
    return ox_text_width_n(win, 0, text);
}

int ox_text_width_n(OxWindow *win, int font_idx, const char *text) {
    if (!text || !*text || font_idx < 0 || font_idx >= win->font_count) return 0;
    XGlyphInfo ext;
    XftTextExtentsUtf8(win->dpy, win->fonts[font_idx], (const FcChar8 *)text, strlen(text), &ext);
    return ext.xOff;
}

void ox_draw_sep(OxWindow *win, int x, int y, const char *sep, const char *fg) {
    ox_draw_text(win, x, y, sep, fg);
}

void ox_draw_xpm(OxWindow *win, int x, int y, const char *path) {
    XpmAttributes attr = {0};
    Pixmap pix, mask;
    if (XpmReadFileToPixmap(win->dpy, RootWindow(win->dpy, win->screen),
            path, &pix, &mask, &attr) == XpmSuccess) {
        XCopyArea(win->dpy, pix, win->win, win->gc, 0, 0, attr.width, attr.height, x, y);
        if (mask) XFreePixmap(win->dpy, mask);
        XFreePixmap(win->dpy, pix);
    }
}

Window ox_window_handle(OxWindow *win) { return win->win; }
void ox_draw_flush(OxWindow *win) { XFlush(win->dpy); }
