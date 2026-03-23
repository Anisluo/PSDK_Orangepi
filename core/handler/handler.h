#ifndef DRONE_HANDLER_H
#define DRONE_HANDLER_H

#include "../server/tcp_server.h"

/*
 * handler.h — JSON-RPC command dispatcher
 *
 * Call handler_set_server() once after tcp_server_create().
 * handle_rpc() is then registered as the tcp_msg_cb (userdata ignored).
 */

/* Must be called once before the server starts accepting connections. */
void handler_set_server(TcpServer *srv);

/* tcp_msg_cb-compatible signature — pass as callback to tcp_server_create() */
int handle_rpc(int client_fd, const char *line, size_t len, void *userdata);

#endif /* DRONE_HANDLER_H */
