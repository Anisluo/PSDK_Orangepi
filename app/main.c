/*
 * main.c — PSDK bridge daemon entry point (OrangePi Zero3)
 *
 * Startup sequence:
 *   1. Parse CLI args (--port, --bind-ip, --debug)
 *   2. Initialise PSDK (HAL + DjiCore)           [BSP]
 *   3. Initialise drone control subscriptions     [BSP]
 *   4. Start UDP server                           [Core]
 *   5. Poll loop until SIGINT/SIGTERM
 *   6. Graceful shutdown
 *
 * Usage:
 *   psdkd [--port <N>] [--bind-ip <IP>] [--debug]
 *
 * Default port: DRONE_SERVER_PORT (5555)
 * Protocol    : JSON-RPC over UDP (one datagram = one request/response)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#include "../core/log/log.h"
#include "../core/server/udp_server.h"
#include "../core/handler/handler.h"
#include "../bsp/psdk_init.h"
#include "../bsp/drone_ctrl.h"
#include "../include/drone_types.h"

#define TAG "app.main"

static volatile int g_running = 1;

static void sig_handler(int sig) {
    (void)sig;
    g_running = 0;
}

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [OPTIONS]\n"
        "  --port     <N>    UDP listen port       (default %d)\n"
        "  --bind-ip  <IP>   IP address to bind to (default 0.0.0.0)\n"
        "  --debug           Enable debug log level\n"
        "  --help            Show this help\n",
        prog, DRONE_SERVER_PORT);
}

int main(int argc, char *argv[]) {
    uint16_t    port    = DRONE_SERVER_PORT;
    const char *bind_ip = "0.0.0.0";
    int         debug   = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--port") && i + 1 < argc) {
            port = (uint16_t)atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--bind-ip") && i + 1 < argc) {
            bind_ip = argv[++i];
        } else if (!strcmp(argv[i], "--debug")) {
            debug = 1;
        } else if (!strcmp(argv[i], "--help")) {
            usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }

    log_set_level(debug ? LOG_DEBUG : LOG_INFO);
    log_info(TAG, "psdkd starting (bind=%s port=%u debug=%d)",
             bind_ip, port, debug);

    /* ── Signal handling ──────────────────────────────────────────────────── */
    struct sigaction sa = { .sa_handler = sig_handler };
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    /* ── BSP: PSDK init ───────────────────────────────────────────────────── */
    if (psdk_init() != 0) {
        log_error(TAG, "PSDK init failed — aborting");
        return 2;
    }

    /* ── BSP: drone control subscriptions ────────────────────────────────── */
    if (drone_ctrl_init() != 0) {
        log_error(TAG, "drone_ctrl_init failed — aborting");
        psdk_deinit();
        return 3;
    }

    /* ── Core: UDP server ─────────────────────────────────────────────────── */
    UdpServer *srv = udp_server_create(bind_ip, port, handle_rpc, NULL);
    if (!srv) {
        log_error(TAG, "udp_server_create failed — aborting");
        drone_ctrl_deinit();
        psdk_deinit();
        return 4;
    }
    handler_set_server(srv);

    /* ── Main event loop ──────────────────────────────────────────────────── */
    log_info(TAG, "ready — listening on UDP port %u", port);

    while (g_running) {
        udp_server_poll(srv, 500 /* ms */);
    }

    /* ── Graceful shutdown ────────────────────────────────────────────────── */
    log_info(TAG, "shutting down...");
    udp_server_destroy(srv);
    drone_ctrl_deinit();
    psdk_deinit();
    log_info(TAG, "bye");
    return 0;
}
