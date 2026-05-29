/*
 * ipc.c — Inter-process communication via Unix domain sockets
 *
 * IPC lets external scripts and programs send commands to a running ox bar.
 * For example, you might want to send "update-vol" from a keybind to
 * refresh the volume widget, or "switch-workspace 3" to change workspaces.
 *
 * HOW IT WORKS:
 *   ox_ipc_init() creates a Unix domain socket in stream mode (SOCK_STREAM).
 *   The socket path is $XDG_RUNTIME_DIR/ox-ipc-$$ where $$ is the PID.
 *   This ensures each bar instance gets its own socket, and the socket
 *   is automatically cleaned up when the process exits.
 *
 *   The server listens for connections. In the event loop, ox_ipc_fd()
 *   returns the server fd so select() can monitor it. When a client
 *   connects, ox_ipc_recv() accepts the connection, reads one line of
 *   text, strips the newline, and returns it. The client connection is
 *   immediately closed after reading — this is a one-shot protocol.
 *
 *   ox_ipc_send() is the client side: it connects to the server socket,
 *   sends a message, and disconnects. This is what external scripts use.
 *
 * PROTOCOL:
 *   Line-based. Messages are terminated by '\n'. Newlines are stripped
 *   on receive. There is no framing, no length prefix, no binary data.
 *   Keep it simple — this is a status bar, not a database.
 *
 * USAGE FROM SCRIPTS:
 *   # Find the socket path (or use a well-known location)
 *   SOCK="/run/user/$(id -u)/ox-ipc-$(pgrep xmobar).sock"
 *
 *   # Send a command
 *   echo "update-vol" | socat - UNIX-CONNECT:path=$SOCK
 *
 *   # Or from C:
 *   ox_ipc_send("update-vol");
 *
 * USAGE IN THE BAR:
 *   In your OxMain setup:
 *     ox_ipc_init();
 *     loop.ipc_fd = ox_ipc_fd();
 *     loop.on_ipc = my_ipc_handler;
 *
 *   Then in my_ipc_handler:
 *     if (strcmp(msg, "update-vol") == 0) ox_widget_update(w_vol);
 *
 * CLEANUP:
 *   ox_ipc_cleanup() closes the server fd and unlinks the socket file.
 *   This is called by ox_cleanup(), which is called after ox_main() returns.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "ox.h"

static int g_server_fd = -1;
static char g_sock_path[108];

int ox_ipc_init(void) {
    const char *runtime = getenv("XDG_RUNTIME_DIR");
    if (!runtime) runtime = "/tmp";
    snprintf(g_sock_path, sizeof(g_sock_path), "%s/ox-ipc-%d.sock", runtime, getpid());

    struct sockaddr_un addr;
    unlink(g_sock_path);
    g_server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (g_server_fd < 0) return -1;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, g_sock_path, sizeof(addr.sun_path) - 1);
    if (bind(g_server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(g_server_fd);
        g_server_fd = -1;
        return -1;
    }
    listen(g_server_fd, 5);
    return 0;
}

int ox_ipc_fd(void) {
    return g_server_fd;
}

int ox_ipc_send(const char *msg) {
    if (g_sock_path[0] == '\0') return -1;
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, g_sock_path, sizeof(addr.sun_path) - 1);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    size_t len = strlen(msg);
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, msg + sent, len - sent, 0);
        if (n <= 0) break;
        sent += n;
    }
    close(fd);
    return (int)sent;
}

int ox_ipc_recv(char *buf, size_t len) {
    if (g_server_fd < 0) return -1;
    struct sockaddr_un addr;
    socklen_t addrlen = sizeof(addr);
    int client = accept(g_server_fd, (struct sockaddr *)&addr, &addrlen);
    if (client < 0) return -1;

    /* read until newline or buffer full */
    size_t total = 0;
    while (total < len - 1) {
        ssize_t n = recv(client, buf + total, len - 1 - total, 0);
        if (n <= 0) break;
        total += n;
        if (buf[total - 1] == '\n') break;
    }
    close(client);

    if (total > 0) {
        /* strip trailing newline/carriage return */
        while (total > 0 && (buf[total-1] == '\n' || buf[total-1] == '\r'))
            total--;
        buf[total] = '\0';
        return (int)total;
    }
    return -1;
}

void ox_ipc_cleanup(void) {
    if (g_server_fd >= 0) { close(g_server_fd); g_server_fd = -1; }
    if (g_sock_path[0]) { unlink(g_sock_path); g_sock_path[0] = '\0'; }
}
