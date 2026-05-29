/*
 * widget.c — Widget state management
 *
 * A widget is the basic unit of data in ox. Each widget has a name (for
 * debugging), a label (the text that gets displayed), and an optional
 * icon prefix. Widgets are what you put inside a bar.
 *
 * HOW WIDGETS WORK:
 *   You create a widget, give it an update callback (or a shell command),
 *   and add it to a bar. The event loop calls ox_widget_update() on each
 *   widget when its interval has elapsed. If the new text differs from
 *   the old text, the widget is marked "dirty" — meaning something changed
 *   and the bar needs to redraw.
 *
 *   This dirty-flag system is the core of ox's efficiency: we never redraw
 *   the whole bar on a timer. We only redraw when a widget actually has
 *   new data. If nothing changes, nothing gets drawn.
 *
 * TWO WAYS TO UPDATE A WIDGET:
 *
 *   1. Callback function (the "C native" way):
 *      You write a function that reads from /proc, /sys, or whatever,
 *      and writes the result into a 256-byte buffer. The library calls
 *      this function on every interval tick.
 *
 *      Example: reading CPU usage from /proc/stat
 *        static void cpu_update(void *ctx, char *buf, size_t len) {
 *            FILE *f = fopen("/proc/stat", "r");
 *            // ... parse, calculate percentage ...
 *            snprintf(buf, len, "%d%%", pct);
 *        }
 *
 *   2. Shell command (the "external" way):
 *      You give the widget a shell command string. The library forks a
 *      child process, runs the command, and reads its stdout via a
 *      non-blocking pipe. When the command finishes, the output becomes
 *      the widget label. This is great for one-liners like:
 *        "amixer get Master | awk -F'[][]' '/%/ {print $2}'"
 *
 *      The fork/exec approach (not popen, not threads) means:
 *      - No blocking: the bar keeps running while the command runs
 *      - No threads: no race conditions, no pthread dependency
 *      - Clean cleanup: child process is tracked and reaped
 *
 * ASYNC COMMAND PIPE:
 *   When you use ox_widget_set_cmd(), the library creates a pipe,
 *   forks a child, and connects the child's stdout to the pipe.
 *   The pipe's read end is set to non-blocking mode (O_NONBLOCK).
 *   The fd is exposed via ox_widget_get_fd() so the event loop can
 *   monitor it with select(). When data arrives, you call
 *   ox_widget_read() to accumulate it. When the child exits (EOF on
 *   the pipe), the accumulated output becomes the widget label.
 *
 * MEMORY OWNERSHIP:
 *   The widget owns: name (strdup'd), icon (strdup'd), fg/bg (strdup'd).
 *   The label is a fixed 256-byte buffer inside the struct.
 *   ox_widget_destroy() frees all owned memory.
 *
 * THREAD SAFETY:
 *   The widget struct is NOT thread-safe. All access must happen from
 *   the event loop thread. The async command uses fork (not threads),
 *   so there are no shared-memory concerns — the child writes to a
 *   pipe, the parent reads from it.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "ox.h"

/* The internal widget struct. Not exposed in ox.h — callers only see OxWidget*. */
struct OxWidget {
    char *name;          /* identifier for debugging */
    char *icon;          /* icon text prefix (e.g. "CPU", "MEM") */
    char label[256];     /* current display text — written by update callback or cmd */
    char *fg;            /* foreground color override (NULL = use bar default) */
    char *bg;            /* background color override (NULL = use bar default) */
    double interval;     /* update interval in seconds (0 = manual only) */
    OxWidgetUpdate update; /* callback that writes new text to buf */
    OxWidgetClick click;   /* callback when widget is clicked */
    void *ctx;           /* user context passed to update/click callbacks */
    double last_update;  /* monotonic timestamp of last update */
    int dirty;           /* 1 if label changed since last clear */
    char *cmd;           /* shell command for async execution (NULL = use callback) */
    pid_t cmd_pid;       /* PID of running child process (-1 if none) */
    int cmd_fd;          /* read end of pipe from child stdout (-1 if none) */
    char cmd_buf[256];   /* accumulation buffer for partial pipe reads */
    int cmd_buf_len;     /* current length of data in cmd_buf */
};

OxWidget *ox_widget_new(const char *name, double interval) {
    OxWidget *w = calloc(1, sizeof(OxWidget));
    w->name = strdup(name);
    w->interval = interval;
    w->cmd_fd = -1;
    w->cmd_pid = -1;
    return w;
}

void ox_widget_destroy(OxWidget *w) {
    if (!w) return;
    if (w->cmd_fd >= 0) close(w->cmd_fd);
    if (w->cmd_pid > 0) {
        kill(w->cmd_pid, SIGTERM);
        waitpid(w->cmd_pid, NULL, 0);
    }
    free(w->name);
    free(w->icon);
    free(w->fg);
    free(w->bg);
    free(w->cmd);
    free(w);
}

void ox_widget_set_update(OxWidget *w, OxWidgetUpdate update, void *ctx) {
    w->update = update;
    w->ctx = ctx;
}

void ox_widget_set_click(OxWidget *w, OxWidgetClick click) {
    w->click = click;
}

void ox_widget_set_icon(OxWidget *w, const char *icon) {
    free(w->icon);
    w->icon = icon ? strdup(icon) : NULL;
}

void ox_widget_set_cmd(OxWidget *w, const char *cmd) {
    free(w->cmd);
    w->cmd = cmd ? strdup(cmd) : NULL;
}

void ox_widget_set_label_text(OxWidget *w, const char *text) {
    if (text) {
        snprintf(w->label, sizeof(w->label), "%s", text);
        w->dirty = 1;
    } else {
        w->label[0] = '\0';
    }
}

void ox_widget_set_colors(OxWidget *w, const char *fg, const char *bg) {
    free(w->fg);
    free(w->bg);
    w->fg = fg ? strdup(fg) : NULL;
    w->bg = bg ? strdup(bg) : NULL;
}

/*
 * cmd_spawn — fork a child process to run the widget's shell command.
 *
 * We create a pipe, fork, connect the child's stdout to the pipe's write end,
 * close the unused read end in the parent, and set the read end to non-blocking.
 * The child gets a new session (setsid) so it doesn't get killed if the bar
 * exits — though we track the PID and send SIGTERM on widget destroy.
 *
 * This is the POSIX way to run external commands without blocking.
 * No popen (which blocks), no threads (which add complexity and race conditions).
 */
static void cmd_spawn(OxWidget *w) {
    if (w->cmd_pid > 0) return;  /* already running */
    if (!w->cmd || !*w->cmd) return;

    int pipefd[2];
    if (pipe(pipefd) < 0) return;

    pid_t pid = fork();
    if (pid == 0) {
        /* child: close read end, redirect stdout to pipe, exec command */
        close(pipefd[0]);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);
        setsid();
        execl("/bin/sh", "sh", "-c", w->cmd, (char *)NULL);
        _exit(127);  /* exec failed */
    }

    /* parent: close write end, keep read end */
    close(pipefd[1]);
    if (pid < 0) { close(pipefd[0]); return; }

    w->cmd_pid = pid;
    w->cmd_fd = pipefd[0];
    fcntl(w->cmd_fd, F_SETFL, O_NONBLOCK);  /* non-blocking reads */
    w->cmd_buf_len = 0;
}

int ox_widget_get_fd(OxWidget *w) {
    return w->cmd_fd;
}

/*
 * ox_widget_read — accumulate data from the async command pipe.
 *
 * Called when the pipe fd is ready (data available or EOF).
 * We read into a temporary buffer, then append to cmd_buf.
 * When we hit EOF (n == 0) or an error, we close the pipe,
 * reap the child, strip trailing newlines from the accumulated
 * output, and compare it to the current label. If different,
 * we update the label and mark the widget dirty.
 *
 * This is the "async command complete" path — the widget gets
 * its new text here, not in the update callback.
 */
void ox_widget_read(OxWidget *w) {
    if (w->cmd_fd < 0) return;
    char tmp[256];
    ssize_t n = read(w->cmd_fd, tmp, sizeof(tmp) - 1);
    if (n > 0) {
        /* append to accumulation buffer */
        tmp[n] = '\0';
        int space = (int)(sizeof(w->cmd_buf) - 1 - w->cmd_buf_len);
        if (space > 0) {
            int copy = (int)n < space ? (int)n : space;
            memcpy(w->cmd_buf + w->cmd_buf_len, tmp, copy);
            w->cmd_buf_len += copy;
            w->cmd_buf[w->cmd_buf_len] = '\0';
        }
    } else if (n == 0 || (n < 0 && errno != EINTR && errno != EAGAIN)) {
        /* EOF or error — command finished */
        close(w->cmd_fd);
        w->cmd_fd = -1;
        waitpid(w->cmd_pid, NULL, 0);
        w->cmd_pid = -1;

        /* strip trailing newlines */
        while (w->cmd_buf_len > 0 &&
               (w->cmd_buf[w->cmd_buf_len-1] == '\n' ||
                w->cmd_buf[w->cmd_buf_len-1] == '\r'))
            w->cmd_buf[--w->cmd_buf_len] = '\0';

        /* only mark dirty if the output actually changed */
        if (strcmp(w->label, w->cmd_buf) != 0) {
            snprintf(w->label, sizeof(w->label), "%s", w->cmd_buf);
            w->dirty = 1;
        }
        w->cmd_buf_len = 0;
        w->cmd_buf[0] = '\0';
    }
}

void ox_widget_update(OxWidget *w) {
    if (w->cmd && *w->cmd) {
        /* async command path: fork and read pipe later */
        cmd_spawn(w);
    } else if (w->update) {
        /* callback path: call the update function directly */
        char buf[256] = {0};
        w->update(w->ctx, buf, sizeof(buf));
        if (strcmp(w->label, buf) != 0) {
            snprintf(w->label, sizeof(w->label), "%s", buf);
            w->dirty = 1;
        }
    }
}

void ox_widget_do_click(OxWidget *w) {
    if (w && w->click) w->click(w->ctx);
}

const char *ox_widget_get_label(OxWidget *w) { return w->label; }
const char *ox_widget_get_icon(OxWidget *w) { return w->icon; }
const char *ox_widget_get_fg(OxWidget *w) { return w->fg; }
const char *ox_widget_get_bg(OxWidget *w) { return w->bg; }
double ox_widget_get_interval(OxWidget *w) { return w->interval; }
double ox_widget_get_last_update(OxWidget *w) { return w->last_update; }
void ox_widget_set_last_update(OxWidget *w, double t) { w->last_update = t; }
int ox_widget_is_dirty(OxWidget *w) { return w->dirty; }
void ox_widget_clear_dirty(OxWidget *w) { w->dirty = 0; }
