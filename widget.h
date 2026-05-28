// widget.h - Widget system
#ifndef WIDGET_H
#define WIDGET_H

// Widget update function type
typedef void (*WidgetUpdate)(void *ctx, char *buf, size_t len);

// Widget click handler type
typedef void (*WidgetClick)(void *ctx);

// Widget structure
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

// Create a widget
Widget *widget_create(const char *name, const char *icon,
                      double interval, WidgetUpdate update,
                      WidgetClick click, void *ctx);

// Destroy a widget
void widget_destroy(Widget *widget);

// Update widget value
void widget_update(Widget *widget);

// Built-in widget creators
Widget *widget_time_create(void);
Widget *widget_date_create(void);
Widget *widget_cpu_create(void);
Widget *widget_mem_create(void);
Widget *widget_disk_create(void);
Widget *widget_net_create(void);
Widget *widget_vol_create(void);
Widget *widget_bright_create(void);
Widget *widget_bat_create(void);
Widget *widget_load_create(void);
Widget *widget_uptime_create(void);

// External widget (runs a command)
Widget *widget_cmd_create(const char *name, const char *icon,
                          const char *cmd, double interval);

#endif // WIDGET_H
