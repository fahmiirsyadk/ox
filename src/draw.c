#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xft/Xft.h>
#include <X11/xpm.h>
#include "ox.h"

struct OxWindow {
    Display *dpy;
    int screen;
    Window win;
    GC gc;
    XftDraw *draw;
    XftFont *font;
    int width, height;
    int x, y;
    char *bg;
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

OxWindow *ox_window_new(int x, int y, int w, int h) {
    OxWindow *win = calloc(1, sizeof(OxWindow));
    win->dpy = g_dpy;
    win->screen = g_screen;
    win->width = w;
    win->height = h;
    win->x = x;
    win->y = y;
    win->bg = strdup("#000000");
    win->font = XftFontOpenName(g_dpy, g_screen, "monospace:size=11");
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

void ox_window_destroy(OxWindow *win) {
    if (!win) return;
    XftDrawDestroy(win->draw);
    XftFontClose(win->dpy, win->font);
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
    XftFontClose(win->dpy, win->font);
    win->font = XftFontOpenName(win->dpy, win->screen, font);
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

void ox_window_show(OxWindow *win) {
    XMapWindow(win->dpy, win->win);
    XSync(win->dpy, False);
    win->dirty = 1;
}

void ox_window_hide(OxWindow *win) {
    XUnmapWindow(win->dpy, win->win);
    XSync(win->dpy, False);
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

void ox_window_set_strut(OxWindow *win, int top, int bottom, int left, int right) {
    unsigned long s[12] = {0};
    s[0] = left; s[1] = right; s[2] = top; s[3] = bottom;
    s[8] = win->x; s[9] = win->x + win->width - 1;
    XChangeProperty(win->dpy, win->win,
        XInternAtom(win->dpy, "_NET_WM_STRUT_PARTIAL", False),
        XA_CARDINAL, 32, PropModeReplace, (unsigned char *)s, 12);
}

void ox_draw_rect(OxWindow *win, int x, int y, int w, int h, const char *color) {
    XSetForeground(win->dpy, win->gc, parse_hex(color));
    XFillRectangle(win->dpy, win->win, win->gc, x, y, w, h);
}

void ox_draw_text(OxWindow *win, int x, int y, const char *text, const char *fg) {
    if (!text || !*text) return;
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
    XftDrawStringUtf8(win->draw, &color, win->font, x, y,
        (const FcChar8 *)text, strlen(text));
    XftColorFree(win->dpy, DefaultVisual(win->dpy, win->screen),
        DefaultColormap(win->dpy, win->screen), &color);
}

int ox_text_width(OxWindow *win, const char *text) {
    if (!text || !*text) return 0;
    XGlyphInfo ext;
    XftTextExtentsUtf8(win->dpy, win->font, (const FcChar8 *)text, strlen(text), &ext);
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
