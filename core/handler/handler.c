/*
 * handler.c — JSON-RPC command dispatcher (UDP edition)
 *
 * Dispatches incoming JSON-RPC requests to drone_ctrl_* functions
 * and sends the response back to the originating UDP peer.
 *
 * Supported methods:
 *   system.ping
 *   drone.get_telemetry      drone.get_battery_info
 *   drone.get_gimbal_angle   drone.get_camera_state
 *   drone.get_rtk_status
 *   drone.set_rth_altitude   drone.set_gimbal_angle
 *   drone.set_camera_mode    drone.set_camera_zoom
 *   drone.set_home_location  drone.set_obstacle_avoidance
 *   drone.shoot_photo        drone.start_recording
 *   drone.stop_recording
 */

#include "handler.h"
#include "../proto/rpc.h"
#include "../log/log.h"
#include "../../include/drone_types.h"
#include "../../bsp/drone_ctrl.h"

#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <time.h>
#include <limits.h>

#define TAG "core.handler"
#define KMZ_BASE_DIR "/home/orangepi/PSDK/kmz_data"

static UdpServer *g_srv = NULL;

void handler_set_server(UdpServer *srv) {
    g_srv = srv;
}

/* ── Uptime ───────────────────────────────────────────────────────────────── */
static struct timespec g_start;
static int g_started = 0;

static uint64_t uptime_ms(void) {
    if (!g_started) {
        clock_gettime(CLOCK_MONOTONIC, &g_start);
        g_started = 1;
    }
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    time_t sec = now.tv_sec - g_start.tv_sec;
    long nsec = now.tv_nsec - g_start.tv_nsec;

    if (nsec < 0) {
        sec -= 1;
        nsec += 1000000000L;
    }

    return (uint64_t) sec * 1000ULL + (uint64_t) nsec / 1000000ULL;
}

/* ── Reply helpers ────────────────────────────────────────────────────────── */
static void send_result(const struct sockaddr_in *peer, int id,
                        const char *result_json) {
    char buf[DRONE_MAX_MSG_LEN];
    int n = rpc_build_result(buf, sizeof(buf), id, result_json);
    if (n > 0)
        udp_server_send(g_srv, peer, buf, (size_t)n);
}

static void send_error(const struct sockaddr_in *peer, int id,
                       const char *msg) {
    char buf[DRONE_MAX_MSG_LEN];
    int n = rpc_build_error(buf, sizeof(buf), id, msg);
    if (n > 0)
        udp_server_send(g_srv, peer, buf, (size_t)n);
}

/* ══════════════════════════════════════════════════════════════════════════
 * Method handlers
 * ══════════════════════════════════════════════════════════════════════════ */

static void h_system_ping(const struct sockaddr_in *peer, int id) {
    char result[128];
    snprintf(result, sizeof(result),
             "{\"pong\":true,\"uptime_ms\":%llu}",
             (unsigned long long)uptime_ms());
    send_result(peer, id, result);
}

/* ── GET: telemetry ───────────────────────────────────────────────────────── */
static void h_drone_get_telemetry(const struct sockaddr_in *peer, int id) {
    DroneTelemetry t;
    if (drone_get_telemetry(&t) != DRONE_OK) {
        send_error(peer, id, "failed to read telemetry");
        return;
    }
    char result[512];
    snprintf(result, sizeof(result),
        "{"
        "\"lat\":%.8f,\"lon\":%.8f,"
        "\"alt_msl_m\":%.2f,\"alt_rel_m\":%.2f,"
        "\"vx_ms\":%.2f,\"vy_ms\":%.2f,\"vz_ms\":%.2f,"
        "\"heading_deg\":%.1f,"
        "\"battery_pct\":%u,\"battery_mv\":%u,"
        "\"gps_sats\":%u,\"gps_fix\":%u,"
        "\"flight_status\":%u,\"motors_on\":%s"
        "}",
        t.lat, t.lon,
        t.alt_msl_m, t.alt_rel_m,
        t.vx_ms, t.vy_ms, t.vz_ms,
        t.heading_deg,
        t.battery_pct, t.battery_mv,
        t.gps_sats, t.gps_fix,
        t.flight_status, t.motors_on ? "true" : "false");
    send_result(peer, id, result);
}

/* ── GET: battery ─────────────────────────────────────────────────────────── */
static void h_drone_get_battery_info(const struct sockaddr_in *peer, int id) {
    DroneBatteryInfo b;
    if (drone_get_battery_info(&b) != DRONE_OK) {
        send_error(peer, id, "failed to read battery info");
        return;
    }
    char result[256];
    snprintf(result, sizeof(result),
        "{\"voltage_mv\":%u,\"current_ma\":%d,"
        "\"remaining_pct\":%u,\"temperature_dc\":%d,\"cycle_count\":%u}",
        b.voltage_mv, b.current_ma,
        b.remaining_pct, b.temperature_dc, b.cycle_count);
    send_result(peer, id, result);
}

/* ── GET: gimbal ──────────────────────────────────────────────────────────── */
static void h_drone_get_gimbal_angle(const struct sockaddr_in *peer, int id) {
    DroneGimbalAngle g;
    if (drone_get_gimbal_angle(&g) != DRONE_OK) {
        send_error(peer, id, "failed to read gimbal angle");
        return;
    }
    char result[128];
    snprintf(result, sizeof(result),
             "{\"pitch\":%.2f,\"roll\":%.2f,\"yaw\":%.2f}",
             g.pitch, g.roll, g.yaw);
    send_result(peer, id, result);
}

/* ── GET: camera state ────────────────────────────────────────────────────── */
static void h_drone_get_camera_state(const struct sockaddr_in *peer, int id) {
    DroneCameraState c;
    if (drone_get_camera_state(&c) != DRONE_OK) {
        send_error(peer, id, "failed to read camera state");
        return;
    }
    char result[128];
    snprintf(result, sizeof(result),
             "{\"mode\":%d,\"is_recording\":%s,\"zoom_factor\":%.2f}",
             c.mode, c.is_recording ? "true" : "false", c.zoom_factor);
    send_result(peer, id, result);
}

/* ── GET: RTK ─────────────────────────────────────────────────────────────── */
static void h_drone_get_rtk_status(const struct sockaddr_in *peer, int id) {
    DroneRtkStatus r;
    if (drone_get_rtk_status(&r) != DRONE_OK) {
        send_error(peer, id, "failed to read RTK status");
        return;
    }
    char result[256];
    snprintf(result, sizeof(result),
        "{\"enabled\":%s,\"fix_type\":%u,\"satellites\":%u,"
        "\"lat\":%.8f,\"lon\":%.8f,\"alt_m\":%.2f}",
        r.enabled ? "true" : "false",
        r.fix_type, r.satellites,
        r.lat, r.lon, r.alt_m);
    send_result(peer, id, result);
}

static void h_drone_upload_kmz(const struct sockaddr_in *peer, int id,
                               json_object *params) {
    const char *name = rpc_param_str(params, "file");
    char path[PATH_MAX];
    DroneError err;

    if (!name || name[0] == '\0') {
        send_error(peer, id, "missing param: file");
        return;
    }

    if (strstr(name, "..") || strchr(name, '/')) {
        send_error(peer, id, "invalid file name");
        return;
    }

    snprintf(path, sizeof(path), "%s/%s", KMZ_BASE_DIR, name);
    err = drone_upload_kmz(path);
    if (err != DRONE_OK) {
        send_error(peer, id, "kmz upload failed");
        return;
    }

    {
        char result[PATH_MAX + 64];
        snprintf(result, sizeof(result),
                 "{\"uploaded\":true,\"file\":\"%s\",\"path\":\"%s\"}",
                 name, path);
        send_result(peer, id, result);
    }
}

/* ── SET: RTH altitude ────────────────────────────────────────────────────── */
static void h_drone_set_rth_altitude(const struct sockaddr_in *peer, int id,
                                      json_object *params) {
    float alt_m = (float)rpc_param_double(params, "altitude_m", -1.0);
    if (alt_m < 0) {
        send_error(peer, id, "missing param: altitude_m");
        return;
    }
    DroneError err = drone_set_rth_altitude(alt_m);
    if (err != DRONE_OK) send_error(peer, id, "PSDK set rth altitude failed");
    else                  send_result(peer, id, NULL);
}

/* ── SET: gimbal angle ────────────────────────────────────────────────────── */
static void h_drone_set_gimbal_angle(const struct sockaddr_in *peer, int id,
                                      json_object *params) {
    DroneGimbalAngle a;
    a.pitch = (float)rpc_param_double(params, "pitch", 0.0);
    a.roll  = (float)rpc_param_double(params, "roll",  0.0);
    a.yaw   = (float)rpc_param_double(params, "yaw",   0.0);
    DroneError err = drone_set_gimbal_angle(&a);
    if (err != DRONE_OK) send_error(peer, id, "PSDK set gimbal angle failed");
    else                  send_result(peer, id, NULL);
}

/* ── SET: camera mode ─────────────────────────────────────────────────────── */
static void h_drone_set_camera_mode(const struct sockaddr_in *peer, int id,
                                     json_object *params) {
    int mode = rpc_param_int(params, "mode", -1);
    if (mode < 0) {
        send_error(peer, id, "missing param: mode (0=photo, 1=video)");
        return;
    }
    DroneError err = drone_set_camera_mode((CameraMode)mode);
    if (err != DRONE_OK) send_error(peer, id, "PSDK set camera mode failed");
    else                  send_result(peer, id, NULL);
}

/* ── SET: camera zoom ─────────────────────────────────────────────────────── */
static void h_drone_set_camera_zoom(const struct sockaddr_in *peer, int id,
                                     json_object *params) {
    float zoom = (float)rpc_param_double(params, "zoom_factor", -1.0);
    if (zoom < 1.0f) {
        send_error(peer, id, "missing or invalid param: zoom_factor (>= 1.0)");
        return;
    }
    DroneError err = drone_set_camera_zoom(zoom);
    if (err != DRONE_OK) send_error(peer, id, "PSDK set camera zoom failed");
    else                  send_result(peer, id, NULL);
}

/* ── SET: home location ───────────────────────────────────────────────────── */
static void h_drone_set_home_location(const struct sockaddr_in *peer, int id,
                                       json_object *params) {
    DroneHomeLocation loc;
    loc.lat   = rpc_param_double(params, "lat", 999.0);
    loc.lon   = rpc_param_double(params, "lon", 999.0);
    loc.alt_m = (float)rpc_param_double(params, "alt_m", 0.0);
    if (loc.lat > 90.0 || loc.lat < -90.0 ||
        loc.lon > 180.0 || loc.lon < -180.0) {
        send_error(peer, id, "missing or invalid params: lat, lon");
        return;
    }
    DroneError err = drone_set_home_location(&loc);
    if (err != DRONE_OK) send_error(peer, id, "PSDK set home location failed");
    else                  send_result(peer, id, NULL);
}

/* ── SET: obstacle avoidance ──────────────────────────────────────────────── */
static void h_drone_set_obstacle_avoidance(const struct sockaddr_in *peer,
                                            int id, json_object *params) {
    int mode = rpc_param_int(params, "mode", -1);
    if (mode < 0) {
        send_error(peer, id, "missing param: mode (0=off, 1=on, 2=brake)");
        return;
    }
    DroneError err = drone_set_obstacle_avoidance((ObstacleAvoidMode)mode);
    if (err != DRONE_OK) send_error(peer, id, "PSDK set obstacle avoidance failed");
    else                  send_result(peer, id, NULL);
}

/* ── Actions ──────────────────────────────────────────────────────────────── */
static void h_drone_shoot_photo(const struct sockaddr_in *peer, int id) {
    DroneError err = drone_shoot_photo();
    if (err != DRONE_OK) send_error(peer, id, "PSDK shoot photo failed");
    else                  send_result(peer, id, NULL);
}

static void h_drone_start_recording(const struct sockaddr_in *peer, int id) {
    DroneError err = drone_start_recording();
    if (err != DRONE_OK) send_error(peer, id, "PSDK start recording failed");
    else                  send_result(peer, id, NULL);
}

static void h_drone_stop_recording(const struct sockaddr_in *peer, int id) {
    DroneError err = drone_stop_recording();
    if (err != DRONE_OK) send_error(peer, id, "PSDK stop recording failed");
    else                  send_result(peer, id, NULL);
}

/* ══════════════════════════════════════════════════════════════════════════
 * Main dispatch
 * ══════════════════════════════════════════════════════════════════════════ */

int handle_rpc(const struct sockaddr_in *peer,
               const char *line, size_t len, void *userdata) {
    (void)userdata;
    (void)len;

    log_debug(TAG, "rx from %s:%d: %s",
              inet_ntoa(peer->sin_addr), ntohs(peer->sin_port), line);

    RpcRequest req;
    if (rpc_parse(line, &req) != 0) {
        log_warn(TAG, "invalid JSON-RPC from %s — ignoring",
                 inet_ntoa(peer->sin_addr));
        return 0;
    }

    const char  *m = req.method;
    int          id = req.id;
    json_object *p  = req.params;

    /* system */
    if      (!strcmp(m, "system.ping"))
        h_system_ping(peer, id);
    /* get */
    else if (!strcmp(m, "drone.get_telemetry"))
        h_drone_get_telemetry(peer, id);
    else if (!strcmp(m, "drone.get_battery_info"))
        h_drone_get_battery_info(peer, id);
    else if (!strcmp(m, "drone.get_gimbal_angle"))
        h_drone_get_gimbal_angle(peer, id);
    else if (!strcmp(m, "drone.get_camera_state"))
        h_drone_get_camera_state(peer, id);
    else if (!strcmp(m, "drone.get_rtk_status"))
        h_drone_get_rtk_status(peer, id);
    else if (!strcmp(m, "drone.upload_kmz"))
        h_drone_upload_kmz(peer, id, p);
    /* set */
    else if (!strcmp(m, "drone.set_rth_altitude"))
        h_drone_set_rth_altitude(peer, id, p);
    else if (!strcmp(m, "drone.set_gimbal_angle"))
        h_drone_set_gimbal_angle(peer, id, p);
    else if (!strcmp(m, "drone.set_camera_mode"))
        h_drone_set_camera_mode(peer, id, p);
    else if (!strcmp(m, "drone.set_camera_zoom"))
        h_drone_set_camera_zoom(peer, id, p);
    else if (!strcmp(m, "drone.set_home_location"))
        h_drone_set_home_location(peer, id, p);
    else if (!strcmp(m, "drone.set_obstacle_avoidance"))
        h_drone_set_obstacle_avoidance(peer, id, p);
    /* actions */
    else if (!strcmp(m, "drone.shoot_photo"))
        h_drone_shoot_photo(peer, id);
    else if (!strcmp(m, "drone.start_recording"))
        h_drone_start_recording(peer, id);
    else if (!strcmp(m, "drone.stop_recording"))
        h_drone_stop_recording(peer, id);
    else {
        log_warn(TAG, "unknown method: %s", m);
        send_error(peer, id, "method not found");
    }

    rpc_request_free(&req);
    return 0;
}
