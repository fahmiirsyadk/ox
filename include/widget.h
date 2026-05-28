#ifndef WIDGET_H
#define WIDGET_H

typedef void (*WidgetUpdate)(void *ctx, char *buf, size_t len);

typedef struct Widget {
    char *name;
    char *icon;
    char *label;
    char *click_cmd;
    double interval;
    WidgetUpdate update;
    void *ctx;
    double last_update;
    int x;
    int w;
} Widget;

Widget *widget_create(const char *name, const char *icon,
                      double interval, WidgetUpdate update, void *ctx);
void widget_destroy(Widget *widget);
void widget_update(Widget *widget);

Widget *widget_time_create(void);
Widget *widget_cmd_create(const char *name, const char *icon,
                          const char *cmd, double interval);

#endif
