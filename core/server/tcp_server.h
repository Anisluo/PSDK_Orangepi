#ifndef TCP_SERVER_H
#define TCP_SERVER_H

#include <stddef.h>

/*
 * tcp_server — epoll-based, non-blocking TCP server
 *
 * Accepts multiple clients (up to DRONE_MAX_CLIENTS).
 * Buffers incoming data per-client and calls on_message() once a full
 * newline-terminated JSON-RPC line is received.
 *
 * on_message(client_fd, line, len, userdata)
 *   - called with a NUL-terminated line (no trailing \n)
 *   - implementation must call tcp_server_send() to write a response
 *   - must NOT close client_fd directly; return non-zero to close it
 */

typedef int (*tcp_msg_cb)(int client_fd, const char *line, size_t len,
                           void *userdata);

typedef struct TcpServer TcpServer;

/* Allocate and initialise the server.  Binds and listens immediately.
 * bind_ip: IP address string to bind to (e.g. "192.168.1.102").
 *          Pass NULL or "0.0.0.0" to bind all interfaces. */
TcpServer *tcp_server_create(const char *bind_ip, unsigned short port,
                              tcp_msg_cb cb, void *userdata);

/* Run one iteration of the epoll event loop (timeout_ms = -1 blocks). */
void tcp_server_poll(TcpServer *srv, int timeout_ms);

/* Send a NUL-terminated string to a connected client (appends \n). */
int tcp_server_send(TcpServer *srv, int client_fd, const char *msg);

/* Gracefully close a client connection. */
void tcp_server_close_client(TcpServer *srv, int client_fd);

/* Destroy the server and free all resources. */
void tcp_server_destroy(TcpServer *srv);

#endif /* TCP_SERVER_H */
