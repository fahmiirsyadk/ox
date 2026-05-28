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

Bar *bar_create(Display *dpy, int screen, ConfigBlock *cfg) {
    Bar *bar = calloc(1, sizeof(Bar));
    if (bar == NULL) {
        return NULL;
    }

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

    bar->width = XDisplayWidth(dpy, screen);

    const char *position = config_get(cfg, "position");
    if (position != NULL && strcmp(position, "bottom") == 0) {
        bar->y = XDisplayHeight(dpy, screen) - bar->height;
    } else {
        bar->y = 0;
    }

    Window root = RootWindow(dpy, screen);
    bar->win = XCreateSimpleWindow(dpy, root,
        0, bar->y, bar->width, bar->height, 0,
        BlackPixel(dpy, screen), BlackPixel(dpy, screen));

    Atom type = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DOCK", False);
    XChangeProperty(dpy, bar->win, XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False),
        XA_ATOM, 32, PropModeReplace, (unsigned char *)&type, 1);

    long strut[4] = {0, 0, bar->height, 0};
    XChangeProperty(dpy, bar->win, XInternAtom(dpy, "_NET_WM_STRUT_PARTIAL", False),
        XA_CARDINAL, 32, PropModeReplace, (unsigned char *)strut, 4);

    XSetWindowAttributes sa;
    sa.override_redirect = True;
    XChangeWindowAttributes(dpy, bar->win, CWOverrideRedirect, &sa);

    XSelectInput(dpy, bar->win, ButtonPressMask);
    XMapWindow(dpy, bar->win);
    XMoveWindow(dpy, bar->win, 0, bar->y);

    bar->buf = XCreatePixmap(dpy, bar->win, bar->width, bar->height,
        DefaultDepth(dpy, screen));
    bar->gc = XCreateGC(dpy, bar->buf, 0, NULL);

    bar->draw = XftDrawCreate(dpy, bar->buf,
        DefaultVisual(dpy, screen), DefaultColormap(dpy, screen));

    const char *font_name = config_get(cfg, "font");
    bar->font = XftFontOpenName(dpy, screen,
        font_name ? font_name : "monospace:size=11");

    int max_widgets = 32;
    bar->widgets = calloc(max_widgets, sizeof(Widget *));
    bar->widget_count = 0;

    for (ConfigBlock *child = cfg->children; child != NULL; child = child->next) {
        if (strcmp(child->type, "widget") == 0) {
            Widget *widget = NULL;
            const char *name = child->name;

            if (strcmp(name, "time") == 0) {
                widget = widget_time_create();
            } else if (strcmp(name, "date") == 0) {
                widget = widget_date_create();
            } else if (strcmp(name, "cpu") == 0) {
                widget = widget_cpu_create();
            } else if (strcmp(name, "mem") == 0) {
                widget = widget_mem_create();
            } else if (strcmp(name, "disk") == 0) {
                widget = widget_disk_create();
            } else if (strcmp(name, "net") == 0) {
                widget = widget_net_create();
            } else if (strcmp(name, "vol") == 0) {
                widget = widget_vol_create();
            } else if (strcmp(name, "bright") == 0) {
                widget = widget_bright_create();
            } else if (strcmp(name, "bat") == 0) {
                widget = widget_bat_create();
            } else if (strcmp(name, "load") == 0) {
                widget = widget_load_create();
            } else if (strcmp(name, "uptime") == 0) {
                widget = widget_uptime_create();
            } else if (strcmp(name, "sep") == 0) {
                widget = widget_cmd_create("sep", NULL, NULL, 0);
            } else {
                const char *cmd = config_get(child, "cmd");
                if (cmd != NULL) {
                    const char *interval_str = config_get(child, "interval");
                    double interval = interval_str ? atof(interval_str) : 5.0;
                    widget = widget_cmd_create(name, config_get(child, "icon"),
                        cmd, interval);
                }
            }

            if (widget != NULL) {
                const char *icon = config_get(child, "icon");
                if (icon != NULL) {
                    free(widget->icon);
                    widget->icon = strdup(icon);
                }

                const char *interval_str = config_get(child, "interval");
                if (interval_str != NULL) {
                    widget->interval = atof(interval_str);
                }

                widget_update(widget);
                bar->widgets[bar->widget_count++] = widget;
            }
        }
    }

    return bar;
}

void bar_destroy(Bar *bar) {
    if (bar == NULL) {
        return;
    }

    for (int i = 0; i < bar->widget_count; i++) {
        widget_destroy(bar->widgets[i]);
    }
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

void bar_render(Bar *bar) {
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
            if (sep_ok) {
                XftDrawStringUtf8(bar->draw, &sep_color, bar->font,
                    x, y, (const unsigned char *)" | ", 3);
            }
            x += ext.xOff + bar->padding;
            continue;
        }

        if (widget->label[0] == '\0') {
            continue;
        }

        char label[256];
        if (widget->icon != NULL) {
            snprintf(label, sizeof(label), "%s %s", widget->icon, widget->label);
        } else {
            snprintf(label, sizeof(label), "%s", widget->label);
        }

        XGlyphInfo ext;
        XftTextExtentsUtf8(bar->dpy, bar->font,
            (const unsigned char *)label, strlen(label), &ext);

        widget->x = x;
        widget->w = ext.xOff;

        if (fg_ok) {
            XftDrawStringUtf8(bar->draw, &fg_color, bar->font,
                x, y, (const unsigned char *)label, strlen(label));
        }

        x += ext.xOff + bar->padding;
    }

    if (fg_ok) {
        XftColorFree(bar->dpy, DefaultVisual(bar->dpy, bar->screen),
            DefaultColormap(bar->dpy, bar->screen), &fg_color);
    }
    if (sep_ok) {
        XftColorFree(bar->dpy, DefaultVisual(bar->dpy, bar->screen),
            DefaultColormap(bar->dpy, bar->screen), &sep_color);
    }

    XCopyArea(bar->dpy, bar->buf, bar->win, bar->gc,
        0, 0, bar->width, bar->height, 0, 0);
}

void bar_click(Bar *bar, int x) {
    for (int i = 0; i < bar->widget_count; i++) {
        Widget *widget = bar->widgets[i];
        if (widget->click != NULL && widget->label[0] != '\0' &&
            x >= widget->x && x < widget->x + widget->w) {
            widget->click(widget->ctx);
        }
    }
}
