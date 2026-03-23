#include "tcp_server.h"
#include "../log/log.h"
#include "../../include/drone_types.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/epoll.h>

#define TAG "core.server"

/* ── Per-client state ─────────────────────────────────────────────────────── */
typedef struct {
    int       fd;
    char      buf[DRONE_RECV_BUF_LEN];
    size_t    buf_len;
} Client;

/* ── Server object ────────────────────────────────────────────────────────── */
struct TcpServer {
    int         listen_fd;
    int         epoll_fd;
    tcp_msg_cb  on_message;
    void       *userdata;
    Client      clients[DRONE_MAX_CLIENTS];
    int         client_count;
    struct epoll_event ev_scratch[DRONE_MAX_CLIENTS + 1];
};

/* ── Helpers ──────────────────────────────────────────────────────────────── */
static int set_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static Client *find_client(TcpServer *srv, int fd) {
    for (int i = 0; i < DRONE_MAX_CLIENTS; i++) {
        if (srv->clients[i].fd == fd) return &srv->clients[i];
    }
    return NULL;
}

static Client *alloc_client(TcpServer *srv, int fd) {
    for (int i = 0; i < DRONE_MAX_CLIENTS; i++) {
        if (srv->clients[i].fd < 0) {
            srv->clients[i].fd      = fd;
            srv->clients[i].buf_len = 0;
            srv->client_count++;
            return &srv->clients[i];
        }
    }
    return NULL; /* no slot */
}

static void free_client(TcpServer *srv, Client *c) {
    epoll_ctl(srv->epoll_fd, EPOLL_CTL_DEL, c->fd, NULL);
    close(c->fd);
    c->fd      = -1;
    c->buf_len = 0;
    srv->client_count--;
}

/* ── Accept a new connection ─────────────────────────────────────────────── */
static void accept_client(TcpServer *srv) {
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    int cfd = accept(srv->listen_fd, (struct sockaddr *)&addr, &addrlen);
    if (cfd < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK)
            log_warn(TAG, "accept() failed: %s", strerror(errno));
        return;
    }

    if (srv->client_count >= DRONE_MAX_CLIENTS) {
        log_warn(TAG, "client limit reached, rejecting %s",
                 inet_ntoa(addr.sin_addr));
        close(cfd);
        return;
    }

    set_nonblock(cfd);

    /* TCP_NODELAY: reduce latency for small JSON-RPC messages */
    int flag = 1;
    setsockopt(cfd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

    Client *c = alloc_client(srv, cfd);
    if (!c) { close(cfd); return; }

    struct epoll_event ev = { .events = EPOLLIN | EPOLLET, .data.fd = cfd };
    if (epoll_ctl(srv->epoll_fd, EPOLL_CTL_ADD, cfd, &ev) < 0) {
        log_error(TAG, "epoll_ctl ADD failed: %s", strerror(errno));
        free_client(srv, c);
        return;
    }

    log_info(TAG, "client connected: %s:%d (fd=%d)",
             inet_ntoa(addr.sin_addr), ntohs(addr.sin_port), cfd);
}

/* ── Drain and dispatch messages from a client ────────────────────────────── */
static void recv_client(TcpServer *srv, Client *c) {
    while (1) {
        size_t space = DRONE_RECV_BUF_LEN - c->buf_len - 1;
        if (space == 0) {
            log_warn(TAG, "fd=%d: receive buffer full, closing", c->fd);
            free_client(srv, c);
            return;
        }

        ssize_t n = read(c->fd, c->buf + c->buf_len, space);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break; /* done for now */
            log_warn(TAG, "fd=%d read error: %s", c->fd, strerror(errno));
            free_client(srv, c);
            return;
        }
        if (n == 0) {
            log_info(TAG, "fd=%d: client disconnected", c->fd);
            free_client(srv, c);
            return;
        }
        c->buf_len += (size_t)n;
    }

    /* Process all complete lines (\n terminated) */
    c->buf[c->buf_len] = '\0';
    char *start = c->buf;
    char *nl;
    while ((nl = memchr(start, '\n', c->buf_len - (size_t)(start - c->buf)))) {
        *nl = '\0';
        size_t line_len = (size_t)(nl - start);

        /* strip \r if present */
        if (line_len > 0 && start[line_len - 1] == '\r') {
            start[--line_len] = '\0';
        }

        if (line_len > 0) {
            int close_after = srv->on_message(c->fd, start, line_len,
                                               srv->userdata);
            if (close_after) {
                free_client(srv, c);
                return;
            }
        }
        start = nl + 1;
    }

    /* Shift remaining partial line to front of buffer */
    size_t remaining = c->buf_len - (size_t)(start - c->buf);
    if (remaining > 0 && start != c->buf)
        memmove(c->buf, start, remaining);
    c->buf_len = remaining;
}

/* ── Public API ───────────────────────────────────────────────────────────── */

TcpServer *tcp_server_create(const char *bind_ip, unsigned short port,
                              tcp_msg_cb cb, void *userdata) {
    TcpServer *srv = calloc(1, sizeof(*srv));
    if (!srv) return NULL;

    srv->on_message = cb;
    srv->userdata   = userdata;
    for (int i = 0; i < DRONE_MAX_CLIENTS; i++) srv->clients[i].fd = -1;

    /* Create and bind listen socket */
    srv->listen_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (srv->listen_fd < 0) {
        log_error(TAG, "socket() failed: %s", strerror(errno));
        free(srv);
        return NULL;
    }

    int opt = 1;
    setsockopt(srv->listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port   = htons(port),
    };

    /* Resolve bind address */
    if (!bind_ip || !strcmp(bind_ip, "0.0.0.0") || !strcmp(bind_ip, "")) {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else if (inet_pton(AF_INET, bind_ip, &addr.sin_addr) != 1) {
        log_error(TAG, "invalid bind IP '%s'", bind_ip);
        close(srv->listen_fd);
        free(srv);
        return NULL;
    }

    if (bind(srv->listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        log_error(TAG, "bind() on %s:%u failed: %s",
                  bind_ip ? bind_ip : "0.0.0.0", port, strerror(errno));
        close(srv->listen_fd);
        free(srv);
        return NULL;
    }

    if (listen(srv->listen_fd, DRONE_RPC_BACKLOG) < 0) {
        log_error(TAG, "listen() failed: %s", strerror(errno));
        close(srv->listen_fd);
        free(srv);
        return NULL;
    }

    /* epoll instance */
    srv->epoll_fd = epoll_create1(0);
    if (srv->epoll_fd < 0) {
        log_error(TAG, "epoll_create1() failed: %s", strerror(errno));
        close(srv->listen_fd);
        free(srv);
        return NULL;
    }

    struct epoll_event ev = { .events = EPOLLIN, .data.fd = srv->listen_fd };
    epoll_ctl(srv->epoll_fd, EPOLL_CTL_ADD, srv->listen_fd, &ev);

    log_info(TAG, "listening on %s:%u",
             bind_ip ? bind_ip : "0.0.0.0", port);
    return srv;
}

void tcp_server_poll(TcpServer *srv, int timeout_ms) {
    int n = epoll_wait(srv->epoll_fd, srv->ev_scratch,
                       DRONE_MAX_CLIENTS + 1, timeout_ms);
    for (int i = 0; i < n; i++) {
        int fd = srv->ev_scratch[i].data.fd;
        if (fd == srv->listen_fd) {
            accept_client(srv);
        } else {
            Client *c = find_client(srv, fd);
            if (c) recv_client(srv, c);
        }
    }
}

int tcp_server_send(TcpServer *srv, int client_fd, const char *msg) {
    (void)srv;
    size_t len = strlen(msg);
    /* write message */
    if (write(client_fd, msg, len) < 0) return -1;
    /* append newline */
    if (write(client_fd, "\n", 1) < 0) return -1;
    return 0;
}

void tcp_server_close_client(TcpServer *srv, int client_fd) {
    Client *c = find_client(srv, client_fd);
    if (c) free_client(srv, c);
}

void tcp_server_destroy(TcpServer *srv) {
    if (!srv) return;
    for (int i = 0; i < DRONE_MAX_CLIENTS; i++) {
        if (srv->clients[i].fd >= 0) close(srv->clients[i].fd);
    }
    close(srv->epoll_fd);
    close(srv->listen_fd);
    free(srv);
}
