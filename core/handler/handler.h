#ifndef DRONE_HANDLER_H
#define DRONE_HANDLER_H

#include "../server/udp_server.h"

/*
 * handler.h — JSON-RPC command dispatcher (UDP edition)
 *
 * Call handler_set_server() once after udp_server_create().
 * handle_rpc() is then registered as the udp_msg_cb.
 *
 * Protocol: one JSON-RPC object per UDP datagram, no framing needed.
 * Request : {"id":N,"method":"string","params":{...}}
 * Response: {"id":N,"result":{...}}   or  {"id":N,"error":"message"}
 */

/* Must be called once before the server starts receiving datagrams. */
void handler_set_server(UdpServer *srv);

/* udp_msg_cb-compatible — register as callback in udp_server_create() */
int handle_rpc(const struct sockaddr_in *peer,
               const char *line, size_t len, void *userdata);

#endif /* DRONE_HANDLER_H */
