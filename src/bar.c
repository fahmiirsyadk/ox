#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xft/Xft.h>

#include "bar.h"
#include "widget.h"
#include "config.h"

typedef Widget *(*WidgetCtor)(void);

typedef struct {
    const char *name;
    WidgetCtor ctor;
} WidgetDef;

static const WidgetDef widget_registry[] = {
    { "time", widget_time_create },
    { NULL,   NULL               },
};

static int bar_measure_width(Bar *bar)
{
    int width = bar->padding * 2;

    for (int i = 0; i < bar->widget_count; i++) {
        Widget *w = bar->widgets[i];

        if (strcmp(w->name, "sep") == 0) {
            XGlyphInfo ext;
            XftTextExtentsUtf8(bar->dpy, bar->font,
                (const unsigned char *)" | ", 3, &ext);
            width += ext.xOff + bar->padding;
            continue;
        }

        if (w->label[0] == '\0')
            continue;

        char label[256];
        if (w->icon)
            snprintf(label, sizeof(label), "%s %s", w->icon, w->label);
        else
            snprintf(label, sizeof(label), "%s", w->label);

        XGlyphInfo ext;
        XftTextExtentsUtf8(bar->dpy, bar->font,
            (const unsigned char *)label, strlen(label), &ext);
        width += ext.xOff + bar->padding;
    }

    return width;
}

Bar *bar_create(Display *dpy, int screen, ConfigBlock *cfg)
{
    Bar *bar = calloc(1, sizeof(Bar));
    if (bar == NULL)
        return NULL;

    bar->dpy = dpy;
    bar->screen = screen;

    const char *height_str = config_get(cfg, "height");
    bar->height = height_str ? atoi(height_str) : 24;

    const char *padding_str = config_get(cfg, "padding");
    bar->padding = padding_str ? atoi(padding_str) : 8;

    const char *fg_val = config_get(cfg, "fg");
    bar->fg = strdup(fg_val ? fg_val : "#ffffff");
    const char *bg_val = config_get(cfg, "bg");
    bar->bg = strdup(bg_val ? bg_val : "#000000");
    const char *sep_val = config_get(cfg, "sep_color");
    bar->sep_color = strdup(sep_val ? sep_val : "#555555");

    const char *align_str = config_get(cfg, "alignment");
    if (align_str && strcmp(align_str, "center") == 0)
        bar->alignment = BAR_ALIGN_CENTER;
    else if (align_str && strcmp(align_str, "right") == 0)
        bar->alignment = BAR_ALIGN_RIGHT;
    else if (align_str && strcmp(align_str, "stretch") == 0)
        bar->alignment = BAR_ALIGN_STRETCH;
    else
        bar->alignment = BAR_ALIGN_LEFT;

    const char *position = config_get(cfg, "position");
    if (position != NULL && strcmp(position, "bottom") == 0)
        bar->y = XDisplayHeight(dpy, screen) - bar->height;
    else
        bar->y = 0;

    const char *font_name = config_get(cfg, "font");
    bar->font = XftFontOpenName(dpy, screen,
        font_name ? font_name : "monospace:size=11");

    int max_widgets = 32;
    bar->widgets = calloc(max_widgets, sizeof(Widget *));
    bar->widget_count = 0;

    for (ConfigBlock *child = cfg->children; child != NULL; child = child->next) {
        if (strcmp(child->type, "widget") != 0)
            continue;

        Widget *widget = NULL;
        const char *name = child->name;

        for (const WidgetDef *d = widget_registry; d->name != NULL; d++) {
            if (strcmp(name, d->name) == 0) {
                widget = d->ctor();
                break;
            }
        }

        if (widget == NULL && strcmp(name, "sep") == 0)
            widget = widget_cmd_create("sep", NULL, NULL, 0);

        if (widget == NULL) {
            const char *cmd = config_get(child, "cmd");
            if (cmd != NULL) {
                const char *interval_str = config_get(child, "interval");
                double interval = interval_str ? atof(interval_str) : 5.0;
                widget = widget_cmd_create(name, config_get(child, "icon"),
                    cmd, interval);
            }
        }

        if (widget == NULL)
            continue;

        const char *icon = config_get(child, "icon");
        if (icon != NULL) {
            free(widget->icon);
            widget->icon = strdup(icon);
        }

        const char *interval_str = config_get(child, "interval");
        if (interval_str != NULL)
            widget->interval = atof(interval_str);

        widget_update(widget);
        bar->widgets[bar->widget_count++] = widget;
    }

    int screen_w = XDisplayWidth(dpy, screen);

    if (bar->alignment == BAR_ALIGN_STRETCH) {
        bar->width = screen_w;
        bar->x = 0;
    } else {
        bar->width = bar_measure_width(bar);

        switch (bar->alignment) {
        case BAR_ALIGN_CENTER:
            bar->x = (screen_w - bar->width) / 2;
            break;
        case BAR_ALIGN_RIGHT:
            bar->x = screen_w - bar->width;
            break;
        default:
            bar->x = 0;
            break;
        }
    }

    Window root = RootWindow(dpy, screen);
    bar->win = XCreateSimpleWindow(dpy, root,
        bar->x, bar->y, bar->width, bar->height, 0,
        BlackPixel(dpy, screen), BlackPixel(dpy, screen));

    Atom type = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DOCK", False);
    XChangeProperty(dpy, bar->win, XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False),
        XA_ATOM, 32, PropModeReplace, (unsigned char *)&type, 1);

    long strut[12] = {0};
    if (bar->y == 0) {
        strut[2] = bar->height;
        strut[8] = bar->x;
        strut[9] = bar->x + bar->width;
        if (bar->alignment == BAR_ALIGN_LEFT || bar->alignment == BAR_ALIGN_STRETCH) {
            strut[0] = bar->x + bar->width;
            strut[4] = 0;
            strut[5] = bar->height;
        }
        if (bar->alignment == BAR_ALIGN_RIGHT || bar->alignment == BAR_ALIGN_STRETCH) {
            strut[1] = screen_w - bar->x;
            strut[6] = 0;
            strut[7] = bar->height;
        }
    } else {
        strut[3] = bar->height;
        strut[10] = bar->x;
        strut[11] = bar->x + bar->width;
        if (bar->alignment == BAR_ALIGN_LEFT || bar->alignment == BAR_ALIGN_STRETCH) {
            strut[0] = bar->x + bar->width;
            strut[4] = bar->y;
            strut[5] = bar->y + bar->height;
        }
        if (bar->alignment == BAR_ALIGN_RIGHT || bar->alignment == BAR_ALIGN_STRETCH) {
            strut[1] = screen_w - bar->x;
            strut[6] = bar->y;
            strut[7] = bar->y + bar->height;
        }
    }
    XChangeProperty(dpy, bar->win, XInternAtom(dpy, "_NET_WM_STRUT_PARTIAL", False),
        XA_CARDINAL, 32, PropModeReplace, (unsigned char *)strut, 12);

    XSetWindowAttributes sa;
    sa.override_redirect = True;
    XChangeWindowAttributes(dpy, bar->win, CWOverrideRedirect, &sa);

    XSelectInput(dpy, bar->win, ButtonPressMask);
    XMapWindow(dpy, bar->win);

    bar->buf = XCreatePixmap(dpy, bar->win, bar->width, bar->height,
        DefaultDepth(dpy, screen));
    bar->gc = XCreateGC(dpy, bar->buf, 0, NULL);

    bar->draw = XftDrawCreate(dpy, bar->buf,
        DefaultVisual(dpy, screen), DefaultColormap(dpy, screen));

    return bar;
}

void bar_destroy(Bar *bar)
{
    if (bar == NULL)
        return;

    for (int i = 0; i < bar->widget_count; i++)
        widget_destroy(bar->widgets[i]);
    free(bar->widgets);

    XftDrawDestroy(bar->draw);
    XftFontClose(bar->dpy, bar->font);
    XFreePixmap(bar->dpy, bar->buf);
    XFreeGC(bar->dpy, bar->gc);
    XDestroyWindow(bar->dpy, bar->win);

    free(bar->fg);
    free(bar->bg);
    free(bar->sep_color);
    free(bar);
}

void bar_render(Bar *bar)
{
    unsigned long bg_pixel = strtoul(bar->bg + 1, NULL, 16);
    XSetForeground(bar->dpy, bar->gc, bg_pixel);
    XFillRectangle(bar->dpy, bar->buf, bar->gc, 0, 0, bar->width, bar->height);

    int x = bar->padding;
    int y = bar->height * 2 / 3;

    XftColor fg_color = {0};
    XftColor sep_color = {0};
    Bool fg_ok = XftColorAllocName(bar->dpy, DefaultVisual(bar->dpy, bar->screen),
        DefaultColormap(bar->dpy, bar->screen), bar->fg, &fg_color);
    Bool sep_ok = XftColorAllocName(bar->dpy, DefaultVisual(bar->dpy, bar->screen),
        DefaultColormap(bar->dpy, bar->screen), bar->sep_color, &sep_color);

    for (int i = 0; i < bar->widget_count; i++) {
        Widget *widget = bar->widgets[i];

        if (strcmp(widget->name, "sep") == 0) {
            XGlyphInfo ext;
            XftTextExtentsUtf8(bar->dpy, bar->font,
                (const unsigned char *)" | ", 3, &ext);
            if (sep_ok)
                XftDrawStringUtf8(bar->draw, &sep_color, bar->font,
                    x, y, (const unsigned char *)" | ", 3);
            x += ext.xOff + bar->padding;
            continue;
        }

        if (widget->label[0] == '\0')
            continue;

        char label[256];
        if (widget->icon != NULL)
            snprintf(label, sizeof(label), "%s %s", widget->icon, widget->label);
        else
            snprintf(label, sizeof(label), "%s", widget->label);

        XGlyphInfo ext;
        XftTextExtentsUtf8(bar->dpy, bar->font,
            (const unsigned char *)label, strlen(label), &ext);

        widget->x = x;
        widget->w = ext.xOff;

        if (fg_ok)
            XftDrawStringUtf8(bar->draw, &fg_color, bar->font,
                x, y, (const unsigned char *)label, strlen(label));

        x += ext.xOff + bar->padding;
    }

    if (fg_ok)
        XftColorFree(bar->dpy, DefaultVisual(bar->dpy, bar->screen),
            DefaultColormap(bar->dpy, bar->screen), &fg_color);
    if (sep_ok)
        XftColorFree(bar->dpy, DefaultVisual(bar->dpy, bar->screen),
            DefaultColormap(bar->dpy, bar->screen), &sep_color);

    XCopyArea(bar->dpy, bar->buf, bar->win, bar->gc,
        0, 0, bar->width, bar->height, 0, 0);
}

void bar_click(Bar *bar, int x)
{
    for (int i = 0; i < bar->widget_count; i++) {
        Widget *widget = bar->widgets[i];
        if (widget->click != NULL && widget->label[0] != '\0' &&
            x >= widget->x && x < widget->x + widget->w)
            widget->click(widget->ctx);
    }
}
