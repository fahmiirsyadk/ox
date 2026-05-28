#ifndef WIDGET_H
#define WIDGET_H

typedef void (*WidgetUpdate)(void *ctx, char *buf, size_t len);
typedef void (*WidgetClick)(void *ctx);

typedef struct Widget {
    char *name;
    char *icon;
    char *label;
    double interval;
    WidgetUpdate update;
    WidgetClick click;
    void *ctx;
    double last_update;
    int x;
    int w;
} Widget;

Widget *widget_create(const char *name, const char *icon,
                      double interval, WidgetUpdate update,
                      WidgetClick click, void *ctx);
void widget_destroy(Widget *widget);
void widget_update(Widget *widget);

Widget *widget_time_create(void);
Widget *widget_cmd_create(const char *name, const char *icon,
                          const char *cmd, double interval);

#endif
