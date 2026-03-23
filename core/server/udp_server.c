/*
 * udp_server.c — connectionless UDP JSON-RPC server
 *
 * Single socket, recvfrom/sendto.  Each datagram = one complete message.
 * Max datagram size is DRONE_MAX_MSG_LEN (see include/drone_types.h).
 */

#include "udp_server.h"
#include "../log/log.h"
#include "../../include/drone_types.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define TAG "core.udp"

struct UdpServer {
    int         fd;
    udp_msg_cb  on_message;
    void       *userdata;
    char        recv_buf[DRONE_MAX_MSG_LEN];
};

UdpServer *udp_server_create(const char *bind_ip, uint16_t port,
                              udp_msg_cb cb, void *userdata) {
    UdpServer *srv = calloc(1, sizeof(*srv));
    if (!srv) return NULL;

    srv->on_message = cb;
    srv->userdata   = userdata;

    srv->fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (srv->fd < 0) {
        log_error(TAG, "socket() failed: %s", strerror(errno));
        free(srv);
        return NULL;
    }

    int opt = 1;
    setsockopt(srv->fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port   = htons(port),
    };
    if (!bind_ip || !strcmp(bind_ip, "") || !strcmp(bind_ip, "0.0.0.0")) {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else if (inet_pton(AF_INET, bind_ip, &addr.sin_addr) != 1) {
        log_error(TAG, "invalid bind IP '%s'", bind_ip);
        close(srv->fd);
        free(srv);
        return NULL;
    }

    if (bind(srv->fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        log_error(TAG, "bind(%s:%u) failed: %s",
                  bind_ip ? bind_ip : "0.0.0.0", port, strerror(errno));
        close(srv->fd);
        free(srv);
        return NULL;
    }

    log_info(TAG, "UDP listening on %s:%u",
             bind_ip ? bind_ip : "0.0.0.0", port);
    return srv;
}

void udp_server_poll(UdpServer *srv, int timeout_ms) {
    struct pollfd pfd = { .fd = srv->fd, .events = POLLIN };
    if (poll(&pfd, 1, timeout_ms) <= 0) return;

    struct sockaddr_in peer;
    socklen_t peer_len = sizeof(peer);

    ssize_t len = recvfrom(srv->fd, srv->recv_buf, sizeof(srv->recv_buf) - 1,
                           0, (struct sockaddr *)&peer, &peer_len);
    if (len <= 0) return;

    srv->recv_buf[len] = '\0';

    /* Strip trailing whitespace / newlines */
    while (len > 0 && (srv->recv_buf[len - 1] == '\n' ||
                        srv->recv_buf[len - 1] == '\r' ||
                        srv->recv_buf[len - 1] == ' '))
        srv->recv_buf[--len] = '\0';

    if (len == 0) return;

    log_debug(TAG, "rx from %s:%d — %s",
              inet_ntoa(peer.sin_addr), ntohs(peer.sin_port), srv->recv_buf);

    srv->on_message(&peer, srv->recv_buf, (size_t)len, srv->userdata);
}

int udp_server_send(UdpServer *srv, const struct sockaddr_in *peer,
                    const char *msg, size_t len) {
    ssize_t n = sendto(srv->fd, msg, len, 0,
                       (const struct sockaddr *)peer, sizeof(*peer));
    if (n < 0) {
        log_warn(TAG, "sendto %s:%d failed: %s",
                 inet_ntoa(peer->sin_addr), ntohs(peer->sin_port),
                 strerror(errno));
        return -1;
    }
    return 0;
}

void udp_server_destroy(UdpServer *srv) {
    if (!srv) return;
    close(srv->fd);
    free(srv);
}
