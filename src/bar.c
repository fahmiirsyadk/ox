#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <ctype.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xft/Xft.h>

#include "bar.h"
#include "oxbar.h"
#include "plugin.h"
#include "config.h"

static unsigned long parse_hex(Display *dpy, int screen, const char *hex)
{
    XftColor c;
    if (XftColorAllocName(dpy, DefaultVisual(dpy, screen),
            DefaultColormap(dpy, screen), hex, &c))
        return c.pixel;
    return strtoul(hex + 1, NULL, 16);
}

static int parse_value(const char *s)
{
    int val = 0;
    while (*s && !isdigit((unsigned char)*s))
        s++;
    while (*s && isdigit((unsigned char)*s)) {
        val = val * 10 + (*s - '0');
        s++;
    }
    return val;
}

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

    const char *type_str = config_get(cfg, "type");
    bar->type = (type_str && strcmp(type_str, "osd") == 0) ? BAR_OSD : BAR_NORMAL;

    const char *orient_str = config_get(cfg, "orientation");
    bar->orientation = (orient_str && strcmp(orient_str, "vertical") == 0)
        ? ORIENT_VERTICAL : ORIENT_HORIZONTAL;

    const char *timeout_str = config_get(cfg, "timeout");
    bar->timeout = timeout_str ? atoi(timeout_str) : 3;
    bar->shown = (bar->type == BAR_NORMAL);

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

        WidgetCtor ctor = plugin_find(name);
        if (ctor)
            widget = ctor();

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

        const char *cmd = config_get(child, "cmd");
        if (cmd != NULL) {
            free(widget->ctx);
            widget->ctx = strdup(cmd);
            widget->update = update_cmd;
        }

        const char *click = config_get(child, "click");
        if (click != NULL)
            widget->click_cmd = strdup(click);

        const char *scroll_up = config_get(child, "scroll_up");
        if (scroll_up != NULL)
            widget->scroll_up = strdup(scroll_up);

        const char *scroll_down = config_get(child, "scroll_down");
        if (scroll_down != NULL)
            widget->scroll_down = strdup(scroll_down);

        const char *wfg = config_get(child, "fg");
        if (wfg != NULL)
            widget->fg = strdup(wfg);

        const char *wbg = config_get(child, "bg");
        if (wbg != NULL)
            widget->bg = strdup(wbg);

        const char *render_str = config_get(child, "render");
        if (render_str && strcmp(render_str, "progress") == 0)
            widget->render = RENDER_PROGRESS;

        widget_update(widget);
        bar->widgets[bar->widget_count++] = widget;
    }

    int screen_w = XDisplayWidth(dpy, screen);

    if (bar->orientation == ORIENT_VERTICAL) {
        const char *width_str = config_get(cfg, "width");
        bar->width = width_str ? atoi(width_str) : 30;
        bar->x = 0;
    } else if (bar->alignment == BAR_ALIGN_STRETCH) {
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
    if (bar->orientation != ORIENT_VERTICAL) {
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
    }
    XChangeProperty(dpy, bar->win, XInternAtom(dpy, "_NET_WM_STRUT_PARTIAL", False),
        XA_CARDINAL, 32, PropModeReplace, (unsigned char *)strut, 12);

    XSetWindowAttributes sa;
    sa.override_redirect = True;
    XChangeWindowAttributes(dpy, bar->win, CWOverrideRedirect, &sa);

    XSelectInput(dpy, bar->win, ButtonPressMask);

    if (bar->type == BAR_OSD)
        XUnmapWindow(dpy, bar->win);
    else
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

void bar_show(Bar *bar)
{
    if (bar->type == BAR_OSD && !bar->shown) {
        XMapWindow(bar->dpy, bar->win);
        bar->shown = 1;
        bar->hide_at = time(NULL) + bar->timeout;
        for (int i = 0; i < bar->widget_count; i++)
            widget_update(bar->widgets[i]);
    }
}

void bar_hide(Bar *bar)
{
    if (bar->type == BAR_OSD && bar->shown) {
        XUnmapWindow(bar->dpy, bar->win);
        bar->shown = 0;
    }
}

static void render_horizontal(Bar *bar)
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
        Widget *w = bar->widgets[i];

        if (strcmp(w->name, "sep") == 0) {
            XGlyphInfo ext;
            XftTextExtentsUtf8(bar->dpy, bar->font,
                (const unsigned char *)" | ", 3, &ext);
            if (sep_ok)
                XftDrawStringUtf8(bar->draw, &sep_color, bar->font,
                    x, y, (const unsigned char *)" | ", 3);
            x += ext.xOff + bar->padding;
            continue;
        }

        if (w->label[0] == '\0')
            continue;

        char label[256];
        if (w->icon != NULL)
            snprintf(label, sizeof(label), "%s %s", w->icon, w->label);
        else
            snprintf(label, sizeof(label), "%s", w->label);

        XGlyphInfo ext;
        XftTextExtentsUtf8(bar->dpy, bar->font,
            (const unsigned char *)label, strlen(label), &ext);

        w->x = x;
        w->w = ext.xOff;

        if (w->bg != NULL) {
            unsigned long wbg = parse_hex(bar->dpy, bar->screen, w->bg);
            XSetForeground(bar->dpy, bar->gc, wbg);
            XFillRectangle(bar->dpy, bar->buf, bar->gc,
                x - bar->padding / 2, 0, ext.xOff + bar->padding, bar->height);
        }

        XftColor wfg;
        Bool wfg_ok = False;
        if (w->fg != NULL)
            wfg_ok = XftColorAllocName(bar->dpy, DefaultVisual(bar->dpy, bar->screen),
                DefaultColormap(bar->dpy, bar->screen), w->fg, &wfg);

        if (wfg_ok)
            XftDrawStringUtf8(bar->draw, &wfg, bar->font,
                x, y, (const unsigned char *)label, strlen(label));
        else if (fg_ok)
            XftDrawStringUtf8(bar->draw, &fg_color, bar->font,
                x, y, (const unsigned char *)label, strlen(label));

        if (wfg_ok)
            XftColorFree(bar->dpy, DefaultVisual(bar->dpy, bar->screen),
                DefaultColormap(bar->dpy, bar->screen), &wfg);

        x += ext.xOff + bar->padding;
    }

    if (fg_ok)
        XftColorFree(bar->dpy, DefaultVisual(bar->dpy, bar->screen),
            DefaultColormap(bar->dpy, bar->screen), &fg_color);
    if (sep_ok)
        XftColorFree(bar->dpy, DefaultVisual(bar->dpy, bar->screen),
            DefaultColormap(bar->dpy, bar->screen), &sep_color);
}

static void render_vertical(Bar *bar)
{
    unsigned long bg_pixel = strtoul(bar->bg + 1, NULL, 16);
    XSetForeground(bar->dpy, bar->gc, bg_pixel);
    XFillRectangle(bar->dpy, bar->buf, bar->gc, 0, 0, bar->width, bar->height);

    int pad = 4;
    int bar_top = pad;
    int bar_bot = bar->height - pad;
    int bar_h = bar_bot - bar_top;

    for (int i = 0; i < bar->widget_count; i++) {
        Widget *w = bar->widgets[i];

        if (w->label[0] == '\0')
            continue;

        if (w->render == RENDER_PROGRESS) {
            int pct = parse_value(w->label);
            if (pct > 100) pct = 100;
            if (pct < 0) pct = 0;
            int fill = bar_h * pct / 100;

            unsigned long wfg = parse_hex(bar->dpy, bar->screen,
                w->fg ? w->fg : bar->fg);
            XSetForeground(bar->dpy, bar->gc, wfg);
            XFillRectangle(bar->dpy, bar->buf, bar->gc,
                pad, bar_bot - fill, bar->width - pad * 2, fill);

            if (w->icon != NULL) {
                XftColor icon_color;
                Bool icon_ok = XftColorAllocName(bar->dpy,
                    DefaultVisual(bar->dpy, bar->screen),
                    DefaultColormap(bar->dpy, bar->screen),
                    w->fg ? w->fg : bar->fg, &icon_color);
                if (icon_ok) {
                    XftDrawStringUtf8(bar->draw, &icon_color, bar->font,
                        pad, bar_top + 14,
                        (const unsigned char *)w->icon, strlen(w->icon));
                    XftColorFree(bar->dpy, DefaultVisual(bar->dpy, bar->screen),
                        DefaultColormap(bar->dpy, bar->screen), &icon_color);
                }
            }
        } else {
            char label[256];
            if (w->icon != NULL)
                snprintf(label, sizeof(label), "%s %s", w->icon, w->label);
            else
                snprintf(label, sizeof(label), "%s", w->label);

            XGlyphInfo ext;
            XftTextExtentsUtf8(bar->dpy, bar->font,
                (const unsigned char *)label, strlen(label), &ext);

            XftColor wfg;
            Bool wfg_ok = XftColorAllocName(bar->dpy,
                DefaultVisual(bar->dpy, bar->screen),
                DefaultColormap(bar->dpy, bar->screen),
                w->fg ? w->fg : bar->fg, &wfg);

            if (wfg_ok)
                XftDrawStringUtf8(bar->draw, &wfg, bar->font,
                    pad, bar_top + 14 + i * 18,
                    (const unsigned char *)label, strlen(label));

            if (wfg_ok)
                XftColorFree(bar->dpy, DefaultVisual(bar->dpy, bar->screen),
                    DefaultColormap(bar->dpy, bar->screen), &wfg);
        }
    }
}

void bar_render(Bar *bar)
{
    if (bar->orientation == ORIENT_VERTICAL)
        render_vertical(bar);
    else
        render_horizontal(bar);

    XCopyArea(bar->dpy, bar->buf, bar->win, bar->gc,
        0, 0, bar->width, bar->height, 0, 0);
}

void bar_click(Bar *bar, int x)
{
    for (int i = 0; i < bar->widget_count; i++) {
        Widget *w = bar->widgets[i];
        if (w->click_cmd == NULL || w->label[0] == '\0')
            continue;
        if (x < w->x || x >= w->x + w->w)
            continue;
        if (fork() == 0) {
            setsid();
            execl("/bin/sh", "sh", "-c", w->click_cmd, NULL);
            _exit(1);
        }
    }
}

void bar_scroll(Bar *bar, int x, int button)
{
    for (int i = 0; i < bar->widget_count; i++) {
        Widget *w = bar->widgets[i];
        if (w->label[0] == '\0')
            continue;
        if (x < w->x || x >= w->x + w->w)
            continue;

        const char *cmd = NULL;
        if (button == 4)
            cmd = w->scroll_up;
        else if (button == 5)
            cmd = w->scroll_down;

        if (cmd != NULL) {
            if (fork() == 0) {
                setsid();
                execl("/bin/sh", "sh", "-c", cmd, NULL);
                _exit(1);
            }
        }
    }
}
