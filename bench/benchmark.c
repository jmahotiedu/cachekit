/*
 * Simple benchmark client: connects to cachekit, runs SET/GET loops, reports ops/sec.
 * Usage: ./benchmark [host] [port] [n_requests] [payload_bytes]
 * Default: localhost 6380 10000 16
 */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef SOCKET sock_t;
#define close_sock(s) closesocket(s)
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int connect_to(const char *host, int port) {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return -1;
#endif
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
#ifdef _WIN32
    if (fd == (int)INVALID_SOCKET) return -1;
#endif
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) <= 0) {
        close_sock(fd);
        return -1;
    }
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close_sock(fd);
        return -1;
    }
    return fd;
}

static int send_all(int fd, const char *buf, size_t len) {
    size_t sent = 0;
    while (sent < len) {
#ifdef _WIN32
        int n = send((SOCKET)fd, buf + sent, (int)(len - sent), 0);
#else
        ssize_t n = send(fd, buf + sent, len - sent, 0);
#endif
        if (n <= 0) return -1;
        sent += (size_t)n;
    }
    return 0;
}

/* Read until we see \r\n (simple string response) or skip bulk $N\r\n...\r\n */
static int skip_response(int fd) {
    char buf[4096];
    int state = 0;
    size_t bulk_remain = 0;
    while (1) {
#ifdef _WIN32
        int n = recv((SOCKET)fd, buf, sizeof(buf), 0);
#else
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
#endif
        if (n <= 0) return -1;
        for (int i = 0; i < n; i++) {
            char c = buf[i];
            if (state == 0) {
                if (c == '+') state = 1;
                else if (c == '$') state = 2;
                else if (c == '-') state = 1;
                else if (c == ':') state = 1;
            } else if (state == 1) {
                if (c == '\r') state = 3;
            } else if (state == 2) {
                if (c >= '0' && c <= '9') bulk_remain = bulk_remain * 10 + (size_t)(c - '0');
                else if (c == '\r') { bulk_remain += 2; state = 4; }
            } else if (state == 3) {
                if (c == '\n') return 0;
                state = 1;
            } else if (state == 4) {
                if (bulk_remain > 0) bulk_remain--;
                else if (c == '\r') state = 3;
            }
        }
    }
}

int main(int argc, char **argv) {
    const char *host = argc > 1 ? argv[1] : "127.0.0.1";
    int port = argc > 2 ? atoi(argv[2]) : 6380;
    int n = argc > 3 ? atoi(argv[3]) : 10000;
    int payload = argc > 4 ? atoi(argv[4]) : 16;
    if (n <= 0 || payload <= 0 || payload > 1024) {
        fprintf(stderr, "usage: benchmark [host] [port] [n_requests] [payload_bytes]\n");
        return 1;
    }

    int fd = connect_to(host, port);
    if (fd < 0) {
        fprintf(stderr, "connect failed\n");
        return 1;
    }

    char *val = malloc((size_t)payload + 1);
    if (!val) { close_sock(fd); return 1; }
    for (int i = 0; i < payload; i++) val[i] = 'x';
    val[payload] = '\0';

    size_t set_req_len = (size_t)(32 + payload * 2);
    char *set_req = malloc(set_req_len);
    if (!set_req) { free(val); close_sock(fd); return 1; }
    int set_len = snprintf(set_req, set_req_len, "*3\r\n$3\r\nSET\r\n$4\r\nkey0\r\n$%d\r\n%s\r\n", payload, val);
    if (set_len <= 0 || (size_t)set_len >= set_req_len) { free(set_req); free(val); close_sock(fd); return 1; }

    const char *get_req = "*2\r\n$3\r\nGET\r\n$4\r\nkey0\r\n";

    clock_t start = clock();
    for (int i = 0; i < n; i++) {
        if (send_all(fd, set_req, (size_t)set_len) != 0) break;
        if (skip_response(fd) != 0) break;
        if (send_all(fd, get_req, strlen(get_req)) != 0) break;
        if (skip_response(fd) != 0) break;
    }
    clock_t end = clock();
    double elapsed = (double)(end - start) / CLOCKS_PER_SEC;
    unsigned long ops = (unsigned long)(n * 2);
    printf("requests: %lu (SET+GET pairs: %d)\n", ops, n);
    printf("payload: %d bytes\n", payload);
    printf("elapsed: %.3f s\n", elapsed);
    printf("ops/sec: %.0f\n", elapsed > 0 ? (double)ops / elapsed : 0);

    free(set_req);
    free(val);
    close_sock(fd);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
