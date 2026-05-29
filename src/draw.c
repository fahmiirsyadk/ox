/*
 * draw.c — X11 window management and drawing primitives
 *
 * This is the layer that talks to X11. It owns the display connection,
 * creates and manages windows, loads Xft fonts, and provides the drawing
 * functions that widgets and bars use to put pixels on screen.
 *
 * GLOBAL STATE:
 *   g_dpy and g_screen are file-scope globals set by ox_init(). Every
 *   window and draw operation references these. There is exactly one
 *   display connection per process — this is standard for X11 programs.
 *   ox_cleanup() closes the display.
 *
 * WINDOW TYPES:
 *   ox_window_new() creates a dock window (_NET_WM_WINDOW_TYPE_DOCK).
 *   These are the "always visible" panels that window managers reserve
 *   space for via struts. Used for status bars.
 *
 *   ox_window_new_floating() creates a dialog window (_NET_WM_WINDOW_TYPE_DIALOG).
 *   These float above other windows and don't reserve screen space.
 *   Used for things like the search/launcher overlay.
 *
 * DRAWING MODEL:
 *   All drawing is immediate-mode — you call ox_draw_rect/ox_draw_text
 *   and it draws right away. There is no retained scene graph. This is
 *   intentional: status bars are simple enough that immediate mode is
 *   clearer, and the dirty-flag system in widgets means we only redraw
 *   when something actually changes.
 *
 *   Colors are always "#RRGGBB" hex strings. The parse_hex() function
 *   below converts them to X11 pixel values. We use a hand-rolled parser
 *   instead of sscanf because this runs on every draw call and sscanf
 *   has significant overhead for such a simple task.
 *
 * FONT MANAGEMENT:
 *   We use Xft for font rendering (antialiased, supports UTF-8).
 *   Each window has a default font (slot 0, set by ox_window_set_font).
 *   Fonts are opened with XftFontOpenName and closed with XftFontClose.
 *   The window owns its fonts and closes them on destroy.
 *
 * TEXT RENDERING:
 *   ox_draw_text allocates an XftColor, draws the string with
 *   XftDrawStringUtf8, and frees the color. This happens on every
 *   text draw call. For a status bar with ~20 text elements per frame,
 *   this is fine. If it ever becomes a bottleneck, we could add a
 *   small color cache (even 4 entries would eliminate most allocations).
 *
 * MEMORY:
 *   ox_window_new() allocates with calloc, strdup's the bg color.
 *   ox_window_destroy() frees fonts, GC, bg string, XftDraw, and
 *   the window struct itself. The caller must not use the window
 *   pointer after destroy.
 */
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
    Display *dpy;         /* X11 display connection (always g_dpy) */
    int screen;           /* default screen number */
    Window win;           /* X11 window ID */
    GC gc;                /* graphics context for drawing */
    XftDraw *draw;        /* Xft draw context for text rendering */
    XftFont *fonts[MAX_FONTS]; /* loaded fonts (slot 0 = default) */
    int font_count;       /* number of loaded fonts */
    int text_offset;      /* vertical offset for text drawing */
    int width, height;    /* window dimensions */
    int x, y;             /* window position (used for strut calculation) */
    char *bg;             /* background color "#RRGGBB" (owned, freed on destroy) */
    int mapped;           /* 1 if window is currently visible */
    int floating;         /* 1 if dialog window, 0 if dock */
};

static Display *g_dpy;
static int g_screen;

Display *ox_display(void) { return g_dpy; }
int ox_screen(void) { return g_screen; }

void ox_init(void) {
    g_dpy = XOpenDisplay(NULL);
    g_screen = DefaultScreen(g_dpy);
}

void ox_cleanup(void) {
    if (g_dpy) { XCloseDisplay(g_dpy); g_dpy = NULL; }
    ox_ipc_cleanup();
}

/*
 * parse_hex — convert "#RRGGBB" string to X11 pixel value.
 *
 * This is a hand-rolled parser because sscanf is slow for this simple
 * task. On a status bar that draws ~20 elements per frame, this matters.
 * The loop shifts left 4 bits and adds each hex digit — standard stuff.
 *
 * Returns 0 if the string is invalid (not starting with '#' or bad chars).
 */
static unsigned long parse_hex(const char *hex) {
    if (!hex || hex[0] != '#') return 0;
    unsigned long val = 0;
    for (int i = 1; hex[i]; i++) {
        char c = hex[i];
        val <<= 4;
        if (c >= '0' && c <= '9') val |= c - '0';
        else if (c >= 'a' && c <= 'f') val |= c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') val |= c - 'A' + 10;
        else return 0;
    }
    return val;
}

OxWindow *ox_window_new(int x, int y, int w, int h) {
    OxWindow *win = calloc(1, sizeof(OxWindow));
    win->dpy = g_dpy;
    win->screen = g_screen;
    win->width = w;
    win->height = h;
    win->x = x;
    win->y = y;
    win->mapped = 1;
    win->bg = strdup("#000000");
    win->fonts[0] = XftFontOpenName(g_dpy, g_screen, "monospace:size=11");
    win->font_count = 1;
    win->gc = XCreateGC(g_dpy, RootWindow(g_dpy, g_screen), 0, NULL);

    win->win = XCreateSimpleWindow(g_dpy, RootWindow(g_dpy, g_screen),
        x, y, w, h, 0, 0, parse_hex(win->bg));

    win->draw = XftDrawCreate(g_dpy, win->win,
        DefaultVisual(g_dpy, g_screen), DefaultColormap(g_dpy, g_screen));

    /* override_redirect prevents the window manager from decorating or moving us */
    XSetWindowAttributes sa;
    sa.override_redirect = True;
    XChangeWindowAttributes(g_dpy, win->win, CWOverrideRedirect, &sa);

    /* tell the WM this is a dock panel (for strut reservation) */
    Atom type = XInternAtom(g_dpy, "_NET_WM_WINDOW_TYPE_DOCK", False);
    XChangeProperty(g_dpy, win->win,
        XInternAtom(g_dpy, "_NET_WM_WINDOW_TYPE", False),
        XA_ATOM, 32, PropModeReplace, (unsigned char *)&type, 1);

    /* we want expose and button press events */
    XSelectInput(g_dpy, win->win, ButtonPressMask | ExposureMask);
    return win;
}

OxWindow *ox_window_new_floating(int x, int y, int w, int h) {
    OxWindow *win = ox_window_new(x, y, w, h);
    win->floating = 1;
    /* override the window type to dialog (floats above other windows) */
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
}

void ox_window_set_font(OxWindow *win, const char *font) {
    XftFontClose(win->dpy, win->fonts[0]);
    win->fonts[0] = XftFontOpenName(win->dpy, win->screen, font);
}

void ox_window_set_strut(OxWindow *win, int top, int bottom, int left, int right) {
    /*
     * _NET_WM_STRUT_PARTIAL reserves screen edges so windows don't overlap us.
     * The array format is: left, right, top, bottom, then 8 more values
     * defining the start/end of each strut. We set left/right struts to
     * span the full height of the bar.
     */
    unsigned long s[12] = {0};
    s[0] = left; s[1] = right; s[2] = top; s[3] = bottom;
    s[8] = win->x; s[9] = win->x + win->width - 1;
    XChangeProperty(win->dpy, win->win,
        XInternAtom(win->dpy, "_NET_WM_STRUT_PARTIAL", False),
        XA_CARDINAL, 32, PropModeReplace, (unsigned char *)s, 12);
}

void ox_window_set_wm_class(OxWindow *win, const char *wm_class, const char *wm_name) {
    XClassHint hint;
    hint.res_name = (char *)wm_name;
    hint.res_class = (char *)wm_class;
    XSetClassHint(win->dpy, win->win, &hint);
    XStoreName(win->dpy, win->win, wm_name);
}

void ox_window_show(OxWindow *win) {
    XMapWindow(win->dpy, win->win);
    XSync(win->dpy, False);
    win->mapped = 1;
}

void ox_window_hide(OxWindow *win) {
    XUnmapWindow(win->dpy, win->win);
    XSync(win->dpy, False);
    win->mapped = 0;
}

Window ox_window_handle(OxWindow *win) { return win->win; }

/*
 * ox_draw_rect — fill a rectangle with a solid color.
 *
 * This is the simplest drawing primitive. We convert the "#RRGGBB" string
 * to a pixel value with parse_hex(), set it as the foreground color on
 * the window's GC, and call XFillRectangle. Used for backgrounds,
 * selection highlights, progress bars, etc.
 */
void ox_draw_rect(OxWindow *win, int x, int y, int w, int h, const char *color) {
    XSetForeground(win->dpy, win->gc, parse_hex(color));
    XFillRectangle(win->dpy, win->win, win->gc, x, y, w, h);
}

/*
 * draw_text_impl — internal text rendering function.
 *
 * This is the workhorse for all text drawing. The flow is:
 *   1. Parse the "#RRGGBB" color into an XRenderColor
 *   2. Allocate an XftColor from the colormap (this is an X11 resource)
 *   3. Draw the string with XftDrawStringUtf8
 *   4. Free the XftColor
 *
 * The text_offset field lets callers shift text vertically (useful for
 * centering text in a bar that has a specific height).
 *
 * We always use fonts[0] (the default font). If you need multiple fonts,
 * you'd extend this function to take a font index.
 */
static void draw_text_impl(OxWindow *win, int x, int y, const char *text, const char *fg) {
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
    int draw_y = y + win->text_offset;
    XftDrawStringUtf8(win->draw, &color, win->fonts[0], x, draw_y,
        (const FcChar8 *)text, strlen(text));
    XftColorFree(win->dpy, DefaultVisual(win->dpy, win->screen),
        DefaultColormap(win->dpy, win->screen), &color);
}

void ox_draw_text(OxWindow *win, int x, int y, const char *text, const char *fg) {
    draw_text_impl(win, x, y, text, fg);
}

/*
 * ox_text_width — measure the pixel width of a string.
 *
 * Used for layout calculations: "how wide is this label so I know
 * where to draw the next one?" XftTextExtentsUtf8 returns a glyph
 * info struct, and we use xOff (the horizontal advance) as the width.
 */
int ox_text_width(OxWindow *win, const char *text) {
    if (!text || !*text) return 0;
    XGlyphInfo ext;
    XftTextExtentsUtf8(win->dpy, win->fonts[0], (const FcChar8 *)text, strlen(text), &ext);
    return ext.xOff;
}

/*
 * ox_draw_xpm — draw an XPM image from a file.
 *
 * XPM is a simple text-based image format. We read it with XpmReadFileToPixmap,
 * copy it to the window with XCopyArea, then free the pixmaps. The XPM files
 * are stored in examples/xmobar/ and contain the icons for the status bar.
 *
 * This reads from disk on every call. For a status bar that redraws on demand
 * (not on a timer), this is fine — we only redraw when something changes.
 */
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
