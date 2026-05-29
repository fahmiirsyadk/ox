#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <dirent.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include "ox.h"

#define WIN_WIDTH 400
#define WIN_HEIGHT 238
#define MAX_QUERY_LEN 1024

typedef struct {
    char **items;
    int item_count;
    int item_capacity;
    char query[MAX_QUERY_LEN];
    char last_query[MAX_QUERY_LEN];
    int selected;
    int *cached_indices;
    int cached_count;
    int needs_refilter;
    OxWindow *win;
} SearchState;

static void add_line(SearchState *s, const char *line) {
    size_t len = strlen(line);
    while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) len--;
    if (len == 0) return;
    if (s->item_count >= s->item_capacity) {
        s->item_capacity = s->item_capacity == 0 ? 1024 : s->item_capacity * 2;
        s->items = realloc(s->items, s->item_capacity * sizeof(char *));
    }
    s->items[s->item_count++] = strndup(line, len);
}

static void load_items(SearchState *s, const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return;
    char *line = NULL;
    size_t len = 0;
    while (getline(&line, &len, f) != -1) add_line(s, line);
    free(line);
    fclose(f);
}

static int fuzzy_match(const char *item, const char *q) {
    if (!*q) return 1;
    while (*q) {
        while (*item && tolower((unsigned char)*item) != tolower((unsigned char)*q)) item++;
        if (!*item) return 0;
        item++; q++;
    }
    return 1;
}

static void filter_items(SearchState *s) {
    s->cached_count = 0;
    int has_query = (*s->query != '\0');
    for (int i = 0; i < s->item_count; i++) {
        if (!has_query || fuzzy_match(s->items[i], s->query))
            s->cached_indices[s->cached_count++] = i;
    }
}

static void update_filter(SearchState *s) {
    if (s->needs_refilter || strcmp(s->query, s->last_query) != 0) {
        filter_items(s);
        strncpy(s->last_query, s->query, MAX_QUERY_LEN);
        s->last_query[MAX_QUERY_LEN - 1] = '\0';
        s->needs_refilter = 0;
        if (s->selected >= s->cached_count)
            s->selected = s->cached_count > 0 ? s->cached_count - 1 : 0;
    }
}

static void render_search(SearchState *s) {
    ox_draw_rect(s->win, 0, 0, WIN_WIDTH, WIN_HEIGHT, "#1a1b26");
    char display[MAX_QUERY_LEN + 32];
    snprintf(display, sizeof(display), "> %s_", s->query);
    ox_draw_text(s->win, 8, 20, display, "#c0caf5");
    if (s->cached_count > 0) {
        int cy = 30 + 20;
        int max_show = 10;
        int start = s->selected >= max_show ? s->selected - max_show + 1 : 0;
        int end = start + max_show;
        if (end > s->cached_count) end = s->cached_count;
        for (int i = start; i < end; i++) {
            int idx = s->cached_indices[i];
            const char *color = (i == s->selected) ? "#7aa2f7" : "#a9b1d6";
            if (i == s->selected)
                ox_draw_rect(s->win, 4, cy - 14, WIN_WIDTH - 8, 20, "#24283b");
            ox_draw_text(s->win, 12, cy, s->items[idx], color);
            cy += 20;
        }
    }
}

static void launch_app(const char *cmd) {
    if (!cmd || cmd[0] == '\0') return;
    pid_t pid = fork();
    if (pid == 0) {
        setsid();
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) { dup2(devnull, STDOUT_FILENO); dup2(devnull, STDERR_FILENO); close(devnull); }
        execlp("/bin/sh", "sh", "-c", cmd, (char *)NULL);
        _exit(127);
    }
}

static int cmp_items(const void *a, const void *b) {
    return strcmp(*(const char **)a, *(const char **)b);
}

static int g_running = 1;

static void cleanup_signal(int sig) {
    (void)sig;
    g_running = 0;
}

static void on_event(OxMain *m, XEvent *ev) {
    SearchState *s = m->ctx;
    if (ev->type == KeyPress) {
        char buf[32];
        KeySym ks;
        XLookupString(&ev->xkey, buf, sizeof(buf), &ks, NULL);
        if (ks == XK_Escape) {
            g_running = 0;
            ox_quit(m);
        } else if (ks == XK_Return) {
            const char *cmd = NULL;
            if (s->cached_count > 0 && s->selected >= 0 && s->selected < s->cached_count)
                cmd = s->items[s->cached_indices[s->selected]];
            else if (s->query[0] != '\0')
                cmd = s->query;
            if (cmd) launch_app(cmd);
            g_running = 0;
            ox_quit(m);
        } else if (ks == XK_BackSpace) {
            int len = strlen(s->query);
            if (len > 0) { s->query[len - 1] = '\0'; s->selected = 0; s->needs_refilter = 1; }
        } else if (ks == XK_Up || (ks == XK_k && ev->xkey.state & ControlMask)) {
            if (s->selected > 0) s->selected--;
        } else if (ks == XK_Down || (ks == XK_j && ev->xkey.state & ControlMask)) {
            if (s->selected < s->cached_count - 1) s->selected++;
        } else if (buf[0] >= 32 && buf[0] < 127) {
            int len = strlen(s->query);
            if (len < MAX_QUERY_LEN - 1) {
                s->query[len] = buf[0];
                s->query[len + 1] = '\0';
                s->selected = 0;
                s->needs_refilter = 1;
            }
        }
        update_filter(s);
        render_search(s);
    }
    if (ev->type == ButtonPress || ev->type == Expose)
        render_search(s);
}

int main(int argc, char *argv[]) {
    const char *item_file = NULL;
    const char *initial_query = NULL;
    int debug = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-q") == 0 && i + 1 < argc) initial_query = argv[++i];
        else if (strcmp(argv[i], "--debug") == 0) debug = 1;
        else item_file = argv[i];
    }

    SearchState state = {0};
    SearchState *s = &state;

    if (item_file) {
        if (strcmp(item_file, "-") == 0) {
            char *line = NULL;
            size_t len = 0;
            while (getline(&line, &len, stdin) != -1) add_line(s, line);
            free(line);
        } else load_items(s, item_file);
    }

    if (s->item_count == 0) {
        const char *path = getenv("PATH");
        if (!path) path = "/usr/bin:/usr/local/bin";
        char *pc = strdup(path);
        char *dir = strtok(pc, ":");
        while (dir) {
            DIR *d = opendir(dir);
            if (d) {
                struct dirent *entry;
                while ((entry = readdir(d)) != NULL)
                    if (entry->d_name[0] != '.') add_line(s, entry->d_name);
                closedir(d);
            }
            dir = strtok(NULL, ":");
        }
        free(pc);
        qsort(s->items, s->item_count, sizeof(char *), cmp_items);
        int new_count = 0;
        for (int i = 0; i < s->item_count; i++) {
            if (i == 0 || strcmp(s->items[i], s->items[i-1]) != 0)
                s->items[new_count++] = s->items[i];
            else free(s->items[i]);
        }
        s->item_count = new_count;
    }

    if (s->item_count == 0) { fprintf(stderr, "no items\n"); return 1; }
    s->cached_indices = malloc(s->item_count * sizeof(int));

    if (debug) {
        if (initial_query) snprintf(s->query, sizeof(s->query), "%s", initial_query);
        update_filter(s);
        printf("items: %d, query: '%s', filtered: %d\n", s->item_count, s->query, s->cached_count);
        for (int i = 0; i < s->cached_count; i++) printf("  %s\n", s->items[s->cached_indices[i]]);
        free(s->cached_indices);
        for (int i = 0; i < s->item_count; i++) free(s->items[i]);
        free(s->items);
        return 0;
    }

    ox_init();
    Display *dpy = ox_display();
    signal(SIGINT, cleanup_signal);
    signal(SIGTERM, cleanup_signal);

    int sw = DisplayWidth(dpy, ox_screen());
    int sh = DisplayHeight(dpy, ox_screen());

    s->win = ox_window_new_floating((sw - WIN_WIDTH) / 2, sh / 4, WIN_WIDTH, WIN_HEIGHT);
    ox_window_set_bg(s->win, "#1a1b26");
    ox_window_set_font(s->win, "monospace:size=12");
    ox_window_set_wm_class(s->win, "ox-search", "ox-search");
    ox_window_show(s->win);

    if (initial_query) {
        snprintf(s->query, sizeof(s->query), "%s", initial_query);
        s->needs_refilter = 1;
    }

    XSelectInput(dpy, ox_window_handle(s->win), ExposureMask | KeyPressMask | ButtonPressMask);
    XGrabKeyboard(dpy, ox_window_handle(s->win), True, GrabModeAsync, GrabModeAsync, CurrentTime);

    update_filter(s);
    render_search(s);

    OxMain loop = { .on_event = on_event, .ctx = s, .timeout_ms = 100 };
    ox_main(&loop);

    XUngrabKeyboard(dpy, CurrentTime);
    ox_window_destroy(s->win);
    ox_cleanup();
    for (int i = 0; i < s->item_count; i++) free(s->items[i]);
    free(s->items);
    free(s->cached_indices);
    return 0;
}
