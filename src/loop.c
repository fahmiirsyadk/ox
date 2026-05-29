/*
 * loop.c — Event loop (ox_main)
 *
 * This is the heart of ox. Every application calls ox_main() to run the
 * event loop, and the loop handles all the plumbing: select(), signals,
 * X11 event pumping, and calling your callbacks at the right time.
 *
 * HOW IT WORKS:
 *   ox_main() blocks in a loop until you call ox_quit(). On each iteration:
 *
 *   1. BUILD FD_SET: We add the X11 connection fd (always), the IPC socket fd
 *      (if initialized), and any extra fds you registered (like widget async
 *      command pipes). This is how select() knows what to watch.
 *
 *   2. SELECT(): We call select() with a computed timeout. This blocks the
 *      process efficiently — no CPU spinning, no nanosleep. The process
 *      wakes up when: (a) an X11 event arrives, (b) IPC has data, (c) a
 *      widget's async command pipe has output, or (d) the timeout expires.
 *
 *   3. ON_TIMEOUT: We call your on_timeout callback with the current monotonic
 *      timestamp. This is where you update widgets (check if their interval
 *      has elapsed, call ox_widget_update, check dirty flags, redraw).
 *
 *   4. ON_IPC: If the IPC socket has data, we accept a connection, read one
 *      line, and call on_ipc with the message.
 *
 *   5. X11 EVENTS: We pump all pending X11 events with XPending/XNextEvent
 *      and call on_event for each one. This handles Expose (redraw),
 *      ButtonPress (clicks), KeyPress (keyboard input), etc.
 *
 *   6. XFLUSH: We flush the X11 output buffer to ensure all drawing
 *      commands are sent to the server.
 *
 * TIMEOUT COMPUTATION:
 *   The timeout is set to timeout_ms (default 100ms). This means the loop
 *   wakes up at least every 100ms even if no events arrive. This is needed
 *   for things like clock updates (1-second interval) — we need to wake up
 *   periodically to check if the interval has elapsed. The 100ms default
 *   gives sub-second responsiveness without wasting CPU.
 *
 * SIGNAL HANDLING:
 *   SIGINT and SIGTERM set OxMain.running = 0, which breaks the loop
 *   cleanly. We use sigaction with SA_RESTART so that select() is
 *   automatically restarted after the signal handler returns.
 *   SIGCHLD is set to SIG_IGN so child processes (from widget async
 *   commands) are automatically reaped by the kernel — no zombie
 *   processes accumulating.
 *
 * WHY NOT JUST LET APPLICATIONS WRITE THEIR OWN LOOP?
 *   They can (and some do, like search.c). But the event loop has a lot
 *   of boilerplate: signal setup, fd_set construction, timeout computation,
 *   X11 event pumping, select() error handling. By owning this, ox lets
 *   applications focus on their actual logic: update widgets, handle
 *   events, redraw when dirty.
 */
#define _POSIX_C_SOURCE 200809L
#include <signal.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/select.h>
#include <X11/Xlib.h>
#include "ox.h"

static OxMain *g_main = NULL;

static void sig_handler(int sig) {
    (void)sig;
    if (g_main) g_main->running = 0;
}

void ox_quit(OxMain *m) {
    m->running = 0;
}

void ox_main(OxMain *m) {
    Display *dpy = ox_display();
    g_main = m;
    m->running = 1;

    /* set up signal handlers */
    struct sigaction sa = { .sa_handler = sig_handler, .sa_flags = SA_RESTART };
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    signal(SIGCHLD, SIG_IGN);  /* auto-reap child processes */

    while (m->running) {
        /* ── Step 1: build fd_set ── */
        fd_set fds;
        FD_ZERO(&fds);
        int xfd = ConnectionNumber(dpy);
        FD_SET(xfd, &fds);
        int maxfd = xfd;

        /* add IPC socket if initialized */
        if (m->ipc_fd >= 0) {
            FD_SET(m->ipc_fd, &fds);
            if (m->ipc_fd > maxfd) maxfd = m->ipc_fd;
        }

        /* add extra fds (widget async command pipes, etc.) */
        for (int i = 0; i < m->extra_fd_count; i++) {
            if (m->extra_fds[i] >= 0) {
                FD_SET(m->extra_fds[i], &fds);
                if (m->extra_fds[i] > maxfd) maxfd = m->extra_fds[i];
            }
        }

        /* ── Step 2: select() ── */
        struct timeval tv;
        if (m->timeout_ms > 0) {
            tv.tv_sec = m->timeout_ms / 1000;
            tv.tv_usec = (m->timeout_ms % 1000) * 1000;
        } else {
            tv.tv_sec = 0;
            tv.tv_usec = 100000; /* 100ms default */
        }

        int ready = select(maxfd + 1, &fds, NULL, NULL, &tv);
        if (ready < 0 && errno != EINTR) break;

        /* ── Step 3: compute current time ── */
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        double cur = now.tv_sec + now.tv_nsec / 1e9;

        /* ── Step 4: on_timeout callback ── */
        if (m->on_timeout) m->on_timeout(m, cur);

        /* ── Step 5: on_ipc callback ── */
        if (m->ipc_fd >= 0 && FD_ISSET(m->ipc_fd, &fds)) {
            char buf[1024];
            int n = ox_ipc_recv(buf, sizeof(buf));
            if (n > 0 && m->on_ipc) m->on_ipc(m, buf);
        }

        /* ── Step 6: pump X11 events ── */
        while (XPending(dpy) > 0) {
            XEvent ev;
            XNextEvent(dpy, &ev);
            if (m->on_event) m->on_event(m, &ev);
        }

        XFlush(dpy);
    }

    g_main = NULL;
}
