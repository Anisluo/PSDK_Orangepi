#ifndef UDP_SERVER_H
#define UDP_SERVER_H

#include <stddef.h>
#include <stdint.h>
#include <netinet/in.h>

/*
 * udp_server — connectionless UDP server
 *
 * Each UDP datagram is treated as one complete JSON-RPC message.
 * on_message() receives the sender's address; call udp_server_send()
 * with that same address to reply.
 *
 * on_message(peer, line, len, userdata)
 *   - called with a NUL-terminated, newline-stripped message
 *   - use udp_server_send() to reply
 *   - return value is currently unused (reserved)
 */

typedef int (*udp_msg_cb)(const struct sockaddr_in *peer,
                          const char *msg, size_t len,
                          void *userdata);

typedef struct UdpServer UdpServer;

/* Allocate, bind and return a new server.
 * bind_ip: dotted-decimal or NULL/"0.0.0.0" for all interfaces. */
UdpServer *udp_server_create(const char *bind_ip, uint16_t port,
                              udp_msg_cb cb, void *userdata);

/* Wait up to timeout_ms for one incoming datagram and dispatch it. */
void udp_server_poll(UdpServer *srv, int timeout_ms);

/* Send msg (len bytes) to peer.  Returns 0 on success, -1 on error. */
int udp_server_send(UdpServer *srv, const struct sockaddr_in *peer,
                    const char *msg, size_t len);

/* Free all resources. */
void udp_server_destroy(UdpServer *srv);

#endif /* UDP_SERVER_H */
