#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include "ox.h"

#define MAX_ITEMS 4096
#define MAX_LEN 256

static char items[MAX_ITEMS][MAX_LEN];
static int item_count = 0;
static char query[MAX_LEN] = "";
static int selected = 0;

static void load_items(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[MAX_LEN];
    while (fgets(line, sizeof(line), f) && item_count < MAX_ITEMS) {
        size_t n = strlen(line);
        while (n > 0 && (line[n-1] == '\n' || line[n-1] == '\r')) line[--n] = '\0';
        if (n > 0) {
            snprintf(items[item_count], MAX_LEN, "%s", line);
            item_count++;
        }
    }
    fclose(f);
}

static int ci_strcasestr(const char *haystack, const char *needle) {
    if (!*needle) return 1;
    for (; *haystack; haystack++) {
        const char *h = haystack, *n = needle;
        while (*h && *n) {
            char a = (*h >= 'A' && *h <= 'Z') ? *h + 32 : *h;
            char b = (*n >= 'A' && *n <= 'Z') ? *n + 32 : *n;
            if (a != b) break;
            h++; n++;
        }
        if (!*n) return 1;
    }
    return 0;
}

static int filter_items(const char *q, char filtered[][MAX_LEN], int *count) {
    *count = 0;
    if (!*q) {
        for (int i = 0; i < item_count && *count < MAX_ITEMS; i++) {
            snprintf(filtered[*count], MAX_LEN, "%s", items[i]);
            (*count)++;
        }
    } else {
        for (int i = 0; i < item_count && *count < MAX_ITEMS; i++) {
            if (ci_strcasestr(items[i], q)) {
                snprintf(filtered[*count], MAX_LEN, "%s", items[i]);
                (*count)++;
            }
        }
    }
    return *count;
}

static void render(OxWindow *win, void *ctx) {
    (void)ctx;
    int w = 400, h = 30 + 10 * 20 + 8;
    ox_draw_rect(win, 0, 0, w, h, "#1a1b26");

    char display[MAX_LEN + 32];
    snprintf(display, sizeof(display), "> %s_", query);
    ox_draw_text(win, 8, 20, display, "#c0caf5");

    char filtered[MAX_ITEMS][MAX_LEN];
    int count = 0;
    filter_items(query, filtered, &count);

    if (count > 0) {
        int cy = 30 + 20;
        int max_show = 10;
        int start = selected >= max_show ? selected - max_show + 1 : 0;
        int end = start + max_show;
        if (end > count) end = count;

        for (int i = start; i < end; i++) {
            const char *color = (i == selected) ? "#7aa2f7" : "#a9b1d6";
            if (i == selected) {
                ox_draw_rect(win, 4, cy - 14, w - 8, 20, "#24283b");
            }
            ox_draw_text(win, 12, cy, filtered[i], color);
            cy += 20;
        }
    }
}

static void run_selected(void) {
    char filtered[MAX_ITEMS][MAX_LEN];
    int count = 0;
    filter_items(query, filtered, &count);
    if (selected >= 0 && selected < count) {
        printf("%s\n", filtered[selected]);
        fflush(stdout);
    }
}

int main(int argc, char *argv[]) {
    const char *item_file = NULL;
    const char *initial_query = NULL;
    int debug = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-q") == 0 && i + 1 < argc) {
            initial_query = argv[++i];
        } else if (strcmp(argv[i], "--debug") == 0) {
            debug = 1;
        } else {
            item_file = argv[i];
        }
    }

    if (item_file) {
        if (strcmp(item_file, "-") == 0) {
            char line[MAX_LEN];
            while (fgets(line, sizeof(line), stdin) && item_count < MAX_ITEMS) {
                size_t n = strlen(line);
                while (n > 0 && (line[n-1] == '\n' || line[n-1] == '\r')) line[--n] = '\0';
                if (n > 0) {
                    snprintf(items[item_count], MAX_LEN, "%s", line);
                    item_count++;
                }
            }
        } else {
            load_items(item_file);
        }
    }
    
    if (item_count == 0) {
        const char *path = getenv("PATH");
        if (!path) path = "/usr/bin:/usr/local/bin";
        char *pc = strdup(path);
        char *dir = strtok(pc, ":");
        while (dir && item_count < MAX_ITEMS) {
            char cmd[512];
            snprintf(cmd, sizeof(cmd), "ls '%s' 2>/dev/null", dir);
            FILE *f = popen(cmd, "r");
            if (f) {
                char line[MAX_LEN];
                while (fgets(line, sizeof(line), f) && item_count < MAX_ITEMS) {
                    size_t n = strlen(line);
                    while (n > 0 && (line[n-1] == '\n' || line[n-1] == '\r')) line[--n] = '\0';
                    if (n > 0) {
                        snprintf(items[item_count], MAX_LEN, "%s", line);
                        item_count++;
                    }
                }
                pclose(f);
            }
            dir = strtok(NULL, ":");
        }
        free(pc);
        for (int i = 0; i < item_count; i++) {
            for (int j = i + 1; j < item_count; ) {
                if (strcmp(items[i], items[j]) == 0) {
                    memmove(items[j], items[j+1], (item_count - j - 1) * sizeof(items[0]));
                    item_count--;
                } else j++;
            }
        }
    }

    if (item_count == 0) {
        fprintf(stderr, "no items\n");
        return 1;
    }

    if (debug) {
        if (initial_query) snprintf(query, sizeof(query), "%s", initial_query);
        char filtered[MAX_ITEMS][MAX_LEN];
        int count = 0;
        filter_items(query, filtered, &count);
        printf("items: %d, query: '%s', filtered: %d\n", item_count, query, count);
        for (int i = 0; i < count; i++) printf("  %s\n", filtered[i]);
        return 0;
    }

    ox_init();
    Display *dpy = ox_display();
    int sw = DisplayWidth(dpy, ox_screen());
    int sh = DisplayHeight(dpy, ox_screen());
    int w = 400, h = 30 + 10 * 20 + 8;
    int x = (sw - w) / 2;
    int y = sh / 4;

    OxWindow *win = ox_window_new_floating(x, y, w, h);
    ox_window_set_bg(win, "#1a1b26");
    ox_window_set_font(win, "monospace:size=12");
    ox_window_set_wm_class(win, "ox-search", "ox-search");
    ox_window_set_render(win, render, NULL);
    ox_window_show(win);

    if (initial_query) {
        snprintf(query, sizeof(query), "%s", initial_query);
        selected = 0;
    }

    XSelectInput(dpy, ox_window_handle(win), ExposureMask | KeyPressMask | ButtonPressMask);
    XGrabKeyboard(dpy, ox_window_handle(win), True, GrabModeAsync, GrabModeAsync, CurrentTime);

    render(win, NULL);

    XEvent ev;
    while (1) {
        XNextEvent(dpy, &ev);

        if (ev.type == KeyPress) {
            char buf[32];
            KeySym ks;
            XLookupString(&ev.xkey, buf, sizeof(buf), &ks, NULL);

            if (ks == XK_Escape) {
                break;
            } else if (ks == XK_Return) {
                run_selected();
                break;
            } else if (ks == XK_BackSpace) {
                int len = strlen(query);
                if (len > 0) query[len - 1] = '\0';
                selected = 0;
            } else if (ks == XK_Up || (ks == XK_k && ev.xkey.state & ControlMask)) {
                if (selected > 0) selected--;
            } else if (ks == XK_Down || (ks == XK_j && ev.xkey.state & ControlMask)) {
                char filtered[MAX_ITEMS][MAX_LEN];
                int count = 0;
                filter_items(query, filtered, &count);
                if (selected < count - 1) selected++;
            } else if (buf[0] >= 32 && buf[0] < 127) {
                int len = strlen(query);
                if (len < MAX_LEN - 1) {
                    query[len] = buf[0];
                    query[len + 1] = '\0';
                }
                selected = 0;
            }

            render(win, NULL);
        }

        if (ev.type == ButtonPress || ev.type == Expose) {
            render(win, NULL);
        }
    }

    XUngrabKeyboard(dpy, CurrentTime);
    ox_window_destroy(win);
    return 0;
}
