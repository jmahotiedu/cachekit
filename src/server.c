#define _POSIX_C_SOURCE 200112L
#include "server.h"
#include "protocol.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef SOCKET ck_socket_t;
#define CK_INVALID_SOCKET INVALID_SOCKET
#define ck_close(s) closesocket(s)
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <sys/select.h>
typedef int ck_socket_t;
#define CK_INVALID_SOCKET (-1)
#define ck_close(s) close(s)
#endif

#define MAX_CLIENTS 64
#define CLIENT_READ_BUF 4096

typedef struct {
    ck_socket_t fd;
    resp_parser_t parser;
    resp_buf_t out_buf;
    size_t out_sent;
    int has_pending_write;
} client_t;

static client_t clients[MAX_CLIENTS];
static int listen_fd = -1;
static int n_clients;

static void client_clear(client_t *c) {
    if (c->fd != CK_INVALID_SOCKET) {
        ck_close(c->fd);
        c->fd = CK_INVALID_SOCKET;
    }
    resp_parser_destroy(&c->parser);
    resp_buf_destroy(&c->out_buf);
    c->out_sent = 0;
    c->has_pending_write = 0;
}

static void client_init(client_t *c) {
    c->fd = CK_INVALID_SOCKET;
    resp_parser_init(&c->parser);
    resp_buf_init(&c->out_buf);
    c->out_sent = 0;
    c->has_pending_write = 0;
}

static int accept_new_client(server_config_t *config, command_ctx_t *ctx) {
    (void)config;
    (void)ctx;
    if (n_clients >= MAX_CLIENTS) return -1;

    struct sockaddr_in peer;
    socklen_t peer_len = sizeof(peer);
#ifdef _WIN32
    ck_socket_t fd = accept((SOCKET)listen_fd, (struct sockaddr *)&peer, &peer_len);
#else
    ck_socket_t fd = accept(listen_fd, (struct sockaddr *)&peer, &peer_len);
#endif
    if (fd == CK_INVALID_SOCKET) return -1;

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].fd == CK_INVALID_SOCKET) {
            client_init(&clients[i]);
            clients[i].fd = fd;
            n_clients++;
            ctx->connected_clients = n_clients;
            return 0;
        }
    }
    ck_close(fd);
    return -1;
}

static void remove_client(int i, command_ctx_t *ctx) {
    client_clear(&clients[i]);
    n_clients--;
    ctx->connected_clients = n_clients;
}

/* parse one command and set response; returns 1 if had a command, 0 otherwise */
static int parse_and_dispatch(client_t *c, command_ctx_t *ctx) {
    resp_value_t *cmd = NULL;
    if (resp_parse(&c->parser, &cmd) != 1) return 0;
    resp_buf_destroy(&c->out_buf);
    resp_buf_init(&c->out_buf);
    command_dispatch(ctx, cmd, &c->out_buf);
    resp_value_free(cmd);
    c->out_sent = 0;
    c->has_pending_write = 1;
    return 1;
}

static int do_read(client_t *c, command_ctx_t *ctx) {
    char buf[CLIENT_READ_BUF];
#ifdef _WIN32
    int n = recv((SOCKET)c->fd, buf, sizeof(buf), 0);
#else
    ssize_t n = recv(c->fd, buf, sizeof(buf), 0);
#endif
    if (n <= 0) return -1;

    resp_parser_feed(&c->parser, buf, (size_t)n);
    parse_and_dispatch(c, ctx);
    return 0;
}

/* returns 0 on success, -1 on error. after send completes, drain parser for pipelined commands */
static int do_write(client_t *c, command_ctx_t *ctx) {
    if (!c->has_pending_write || c->out_sent >= c->out_buf.len) {
        c->has_pending_write = 0;
        if (c->out_sent >= c->out_buf.len && c->out_buf.len > 0)
            parse_and_dispatch(c, ctx);
        return 0;
    }
    size_t remaining = c->out_buf.len - c->out_sent;
#ifdef _WIN32
    int n = send((SOCKET)c->fd, c->out_buf.buf + c->out_sent, (int)remaining, 0);
#else
    ssize_t n = send(c->fd, c->out_buf.buf + c->out_sent, remaining, 0);
#endif
    if (n <= 0) return -1;
    c->out_sent += (size_t)n;
    if (c->out_sent >= c->out_buf.len) {
        c->has_pending_write = 0;
        parse_and_dispatch(c, ctx);
    }
    return 0;
}

void server_run(server_config_t *config, command_ctx_t *ctx) {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        ck_log(CK_LOG_ERROR, "WSAStartup failed");
        return;
    }
#endif

    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == CK_INVALID_SOCKET) {
        ck_log(CK_LOG_ERROR, "socket() failed");
#ifdef _WIN32
        WSACleanup();
#endif
        return;
    }

    int opt = 1;
#ifdef _WIN32
    setsockopt((SOCKET)listen_fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));
#else
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(config->port);

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        ck_log(CK_LOG_ERROR, "bind() failed");
        ck_close(listen_fd);
#ifdef _WIN32
        WSACleanup();
#endif
        return;
    }

    if (listen(listen_fd, 8) != 0) {
        ck_log(CK_LOG_ERROR, "listen() failed");
        ck_close(listen_fd);
#ifdef _WIN32
        WSACleanup();
#endif
        return;
    }

    ck_log(CK_LOG_INFO, "cachekit listening on port %u", (unsigned)config->port);

    for (int i = 0; i < MAX_CLIENTS; i++)
        clients[i].fd = CK_INVALID_SOCKET;
    n_clients = 0;

    for (;;) {
        fd_set rd, wr;
        FD_ZERO(&rd);
        FD_ZERO(&wr);

#ifdef _WIN32
        FD_SET((SOCKET)listen_fd, &rd);
        ck_socket_t max_fd = listen_fd;
#else
        FD_SET(listen_fd, &rd);
        int max_fd = listen_fd;
#endif

        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].fd == CK_INVALID_SOCKET) continue;
#ifdef _WIN32
            FD_SET((SOCKET)clients[i].fd, &rd);
            if (clients[i].has_pending_write)
                FD_SET((SOCKET)clients[i].fd, &wr);
            if ((ck_socket_t)clients[i].fd > max_fd) max_fd = clients[i].fd;
#else
            FD_SET(clients[i].fd, &rd);
            if (clients[i].has_pending_write)
                FD_SET(clients[i].fd, &wr);
            if (clients[i].fd > max_fd) max_fd = clients[i].fd;
#endif
        }

#ifdef _WIN32
        struct timeval tv = { 1, 0 };
        int n = select((int)(max_fd + 1), &rd, &wr, NULL, &tv);
#else
        struct timeval tv = { 1, 0 };
        int n = select(max_fd + 1, &rd, &wr, NULL, &tv);
#endif

        if (n < 0) {
            ck_log(CK_LOG_ERROR, "select() failed");
            break;
        }
        if (n == 0) continue;

#ifdef _WIN32
        if (FD_ISSET((SOCKET)listen_fd, &rd)) {
#else
        if (FD_ISSET(listen_fd, &rd)) {
#endif
            accept_new_client(config, ctx);
        }

        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].fd == CK_INVALID_SOCKET) continue;

#ifdef _WIN32
            if (FD_ISSET((SOCKET)clients[i].fd, &wr)) {
                if (do_write(&clients[i], ctx) < 0) {
                    remove_client(i, ctx);
                    continue;
                }
            }
            if (FD_ISSET((SOCKET)clients[i].fd, &rd)) {
#else
            if (FD_ISSET(clients[i].fd, &wr)) {
                if (do_write(&clients[i], ctx) < 0) {
                    remove_client(i, ctx);
                    continue;
                }
            }
            if (FD_ISSET(clients[i].fd, &rd)) {
#endif
                if (do_read(&clients[i], ctx) < 0) {
                    remove_client(i, ctx);
                }
            }
        }
    }

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].fd != CK_INVALID_SOCKET)
            client_clear(&clients[i]);
    }
    if (listen_fd != CK_INVALID_SOCKET) {
        ck_close(listen_fd);
        listen_fd = -1;
    }
#ifdef _WIN32
    WSACleanup();
#endif
}
