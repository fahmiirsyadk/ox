#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "ox.h"

#define OX_SOCK_PATH "/tmp/ox-ipc.sock"

static int g_server_fd = -1;
static int g_client_fd = -1;

int ox_ipc_init(void) {
    struct sockaddr_un addr;
    unlink(OX_SOCK_PATH);
    g_server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (g_server_fd < 0) return -1;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, OX_SOCK_PATH, sizeof(addr.sun_path) - 1);
    if (bind(g_server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(g_server_fd);
        g_server_fd = -1;
        return -1;
    }
    listen(g_server_fd, 1);
    return 0;
}

int ox_ipc_send(const char *msg) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, OX_SOCK_PATH, sizeof(addr.sun_path) - 1);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    send(fd, msg, strlen(msg), 0);
    close(fd);
    return 0;
}

int ox_ipc_recv(char *buf, size_t len) {
    if (g_server_fd < 0) return -1;
    struct sockaddr_un addr;
    socklen_t addrlen = sizeof(addr);
    int client = accept(g_server_fd, (struct sockaddr *)&addr, &addrlen);
    if (client < 0) return -1;
    ssize_t n = recv(client, buf, len - 1, 0);
    close(client);
    if (n > 0) { buf[n] = '\0'; return (int)n; }
    return -1;
}
