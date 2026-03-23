#include "handler.h"
#include "../proto/rpc.h"
#include "../log/log.h"
#include "../../include/drone_types.h"
#include "../../bsp/drone_ctrl.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#define TAG "core.handler"

static TcpServer *g_srv = NULL;

void handler_set_server(TcpServer *srv) {
    g_srv = srv;
}

/* Uptime reference (set at first call) */
static struct timespec g_start;
static int g_started = 0;

static uint64_t uptime_ms(void) {
    if (!g_started) {
        clock_gettime(CLOCK_MONOTONIC, &g_start);
        g_started = 1;
    }
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)(now.tv_sec  - g_start.tv_sec)  * 1000ULL
         + (uint64_t)(now.tv_nsec - g_start.tv_nsec) / 1000000ULL;
}

/* ── Helper: send result or error ─────────────────────────────────────────── */
static void send_result(TcpServer *srv, int fd, int id, const char *result_json) {
    char buf[DRONE_MAX_MSG_LEN];
    rpc_build_result(buf, sizeof(buf), id, result_json);
    tcp_server_send(srv, fd, buf);
}

static void send_error(TcpServer *srv, int fd, int id, const char *msg) {
    char buf[DRONE_MAX_MSG_LEN];
    rpc_build_error(buf, sizeof(buf), id, msg);
    tcp_server_send(srv, fd, buf);
}

/* ── system.ping ──────────────────────────────────────────────────────────── */
static void h_system_ping(TcpServer *srv, int fd, int id) {
    char result[128];
    snprintf(result, sizeof(result),
             "{\"pong\":true,\"uptime_ms\":%llu}",
             (unsigned long long)uptime_ms());
    send_result(srv, fd, id, result);
}

/* ── drone.get_telemetry ──────────────────────────────────────────────────── */
static void h_drone_get_telemetry(TcpServer *srv, int fd, int id) {
    DroneTelemetry t;
    DroneError err = drone_get_telemetry(&t);
    if (err != DRONE_OK) {
        send_error(srv, fd, id, "failed to read telemetry");
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
    send_result(srv, fd, id, result);
}

/* ── drone.get_battery_info ───────────────────────────────────────────────── */
static void h_drone_get_battery_info(TcpServer *srv, int fd, int id) {
    DroneBatteryInfo b;
    DroneError err = drone_get_battery_info(&b);
    if (err != DRONE_OK) {
        send_error(srv, fd, id, "failed to read battery info");
        return;
    }
    char result[256];
    snprintf(result, sizeof(result),
        "{\"voltage_mv\":%u,\"current_ma\":%d,"
        "\"remaining_pct\":%u,\"temperature_dc\":%d,\"cycle_count\":%u}",
        b.voltage_mv, b.current_ma,
        b.remaining_pct, b.temperature_dc, b.cycle_count);
    send_result(srv, fd, id, result);
}

/* ── drone.get_gimbal_angle ───────────────────────────────────────────────── */
static void h_drone_get_gimbal_angle(TcpServer *srv, int fd, int id) {
    DroneGimbalAngle g;
    DroneError err = drone_get_gimbal_angle(&g);
    if (err != DRONE_OK) {
        send_error(srv, fd, id, "failed to read gimbal angle");
        return;
    }
    char result[128];
    snprintf(result, sizeof(result),
             "{\"pitch\":%.2f,\"roll\":%.2f,\"yaw\":%.2f}",
             g.pitch, g.roll, g.yaw);
    send_result(srv, fd, id, result);
}

/* ── drone.get_camera_state ───────────────────────────────────────────────── */
static void h_drone_get_camera_state(TcpServer *srv, int fd, int id) {
    DroneCameraState c;
    DroneError err = drone_get_camera_state(&c);
    if (err != DRONE_OK) {
        send_error(srv, fd, id, "failed to read camera state");
        return;
    }
    char result[128];
    snprintf(result, sizeof(result),
             "{\"mode\":%d,\"is_recording\":%s,\"zoom_factor\":%.2f}",
             c.mode, c.is_recording ? "true" : "false", c.zoom_factor);
    send_result(srv, fd, id, result);
}

/* ── drone.get_rtk_status ─────────────────────────────────────────────────── */
static void h_drone_get_rtk_status(TcpServer *srv, int fd, int id) {
    DroneRtkStatus r;
    DroneError err = drone_get_rtk_status(&r);
    if (err != DRONE_OK) {
        send_error(srv, fd, id, "failed to read RTK status");
        return;
    }
    char result[256];
    snprintf(result, sizeof(result),
        "{\"enabled\":%s,\"fix_type\":%u,\"satellites\":%u,"
        "\"lat\":%.8f,\"lon\":%.8f,\"alt_m\":%.2f}",
        r.enabled ? "true" : "false",
        r.fix_type, r.satellites,
        r.lat, r.lon, r.alt_m);
    send_result(srv, fd, id, result);
}

/* ── drone.set_rth_altitude ───────────────────────────────────────────────── */
static void h_drone_set_rth_altitude(TcpServer *srv, int fd, int id,
                                      json_object *params) {
    float alt_m = (float)rpc_param_double(params, "altitude_m", -1.0);
    if (alt_m < 0) {
        send_error(srv, fd, id, "missing param: altitude_m");
        return;
    }
    DroneError err = drone_set_rth_altitude(alt_m);
    if (err != DRONE_OK) send_error(srv, fd, id, "PSDK set rth altitude failed");
    else                  send_result(srv, fd, id, NULL);
}

/* ── drone.set_gimbal_angle ───────────────────────────────────────────────── */
static void h_drone_set_gimbal_angle(TcpServer *srv, int fd, int id,
                                      json_object *params) {
    DroneGimbalAngle a;
    a.pitch = (float)rpc_param_double(params, "pitch", 0.0);
    a.roll  = (float)rpc_param_double(params, "roll",  0.0);
    a.yaw   = (float)rpc_param_double(params, "yaw",   0.0);
    DroneError err = drone_set_gimbal_angle(&a);
    if (err != DRONE_OK) send_error(srv, fd, id, "PSDK set gimbal angle failed");
    else                  send_result(srv, fd, id, NULL);
}

/* ── drone.set_camera_mode ────────────────────────────────────────────────── */
static void h_drone_set_camera_mode(TcpServer *srv, int fd, int id,
                                     json_object *params) {
    int mode = rpc_param_int(params, "mode", -1);
    if (mode < 0) {
        send_error(srv, fd, id, "missing param: mode (0=photo, 1=video)");
        return;
    }
    DroneError err = drone_set_camera_mode((CameraMode)mode);
    if (err != DRONE_OK) send_error(srv, fd, id, "PSDK set camera mode failed");
    else                  send_result(srv, fd, id, NULL);
}

/* ── drone.set_camera_zoom ────────────────────────────────────────────────── */
static void h_drone_set_camera_zoom(TcpServer *srv, int fd, int id,
                                     json_object *params) {
    float zoom = (float)rpc_param_double(params, "zoom_factor", -1.0);
    if (zoom < 1.0f) {
        send_error(srv, fd, id, "missing or invalid param: zoom_factor (>= 1.0)");
        return;
    }
    DroneError err = drone_set_camera_zoom(zoom);
    if (err != DRONE_OK) send_error(srv, fd, id, "PSDK set camera zoom failed");
    else                  send_result(srv, fd, id, NULL);
}

/* ── drone.set_home_location ──────────────────────────────────────────────── */
static void h_drone_set_home_location(TcpServer *srv, int fd, int id,
                                       json_object *params) {
    DroneHomeLocation loc;
    loc.lat   = rpc_param_double(params, "lat", 999.0);
    loc.lon   = rpc_param_double(params, "lon", 999.0);
    loc.alt_m = (float)rpc_param_double(params, "alt_m", 0.0);
    if (loc.lat > 90.0 || loc.lat < -90.0 ||
        loc.lon > 180.0 || loc.lon < -180.0) {
        send_error(srv, fd, id, "missing or invalid params: lat, lon");
        return;
    }
    DroneError err = drone_set_home_location(&loc);
    if (err != DRONE_OK) send_error(srv, fd, id, "PSDK set home location failed");
    else                  send_result(srv, fd, id, NULL);
}

/* ── drone.set_obstacle_avoidance ─────────────────────────────────────────── */
static void h_drone_set_obstacle_avoidance(TcpServer *srv, int fd, int id,
                                            json_object *params) {
    int mode = rpc_param_int(params, "mode", -1);
    if (mode < 0) {
        send_error(srv, fd, id,
                   "missing param: mode (0=off, 1=on, 2=brake)");
        return;
    }
    DroneError err = drone_set_obstacle_avoidance((ObstacleAvoidMode)mode);
    if (err != DRONE_OK) send_error(srv, fd, id, "PSDK set obstacle avoidance failed");
    else                  send_result(srv, fd, id, NULL);
}

/* ── drone.shoot_photo ────────────────────────────────────────────────────── */
static void h_drone_shoot_photo(TcpServer *srv, int fd, int id) {
    DroneError err = drone_shoot_photo();
    if (err != DRONE_OK) send_error(srv, fd, id, "PSDK shoot photo failed");
    else                  send_result(srv, fd, id, NULL);
}

/* ── drone.start_recording ────────────────────────────────────────────────── */
static void h_drone_start_recording(TcpServer *srv, int fd, int id) {
    DroneError err = drone_start_recording();
    if (err != DRONE_OK) send_error(srv, fd, id, "PSDK start recording failed");
    else                  send_result(srv, fd, id, NULL);
}

/* ── drone.stop_recording ─────────────────────────────────────────────────── */
static void h_drone_stop_recording(TcpServer *srv, int fd, int id) {
    DroneError err = drone_stop_recording();
    if (err != DRONE_OK) send_error(srv, fd, id, "PSDK stop recording failed");
    else                  send_result(srv, fd, id, NULL);
}

/* ── Dispatch table ───────────────────────────────────────────────────────── */

int handle_rpc(int client_fd, const char *line, size_t len, void *userdata) {
    (void)userdata;
    TcpServer *srv = g_srv;
    (void)len;

    log_debug(TAG, "fd=%d rx: %s", client_fd, line);

    RpcRequest req;
    if (rpc_parse(line, &req) != 0) {
        /* Can't respond without an id — just log and carry on */
        log_warn(TAG, "fd=%d: invalid JSON-RPC, ignoring", client_fd);
        return 0;
    }

    const char *m = req.method;
    int id        = req.id;
    json_object *p = req.params;

    /* system */
    if      (!strcmp(m, "system.ping"))
        h_system_ping(srv, client_fd, id);
    /* get */
    else if (!strcmp(m, "drone.get_telemetry"))
        h_drone_get_telemetry(srv, client_fd, id);
    else if (!strcmp(m, "drone.get_battery_info"))
        h_drone_get_battery_info(srv, client_fd, id);
    else if (!strcmp(m, "drone.get_gimbal_angle"))
        h_drone_get_gimbal_angle(srv, client_fd, id);
    else if (!strcmp(m, "drone.get_camera_state"))
        h_drone_get_camera_state(srv, client_fd, id);
    else if (!strcmp(m, "drone.get_rtk_status"))
        h_drone_get_rtk_status(srv, client_fd, id);
    /* set */
    else if (!strcmp(m, "drone.set_rth_altitude"))
        h_drone_set_rth_altitude(srv, client_fd, id, p);
    else if (!strcmp(m, "drone.set_gimbal_angle"))
        h_drone_set_gimbal_angle(srv, client_fd, id, p);
    else if (!strcmp(m, "drone.set_camera_mode"))
        h_drone_set_camera_mode(srv, client_fd, id, p);
    else if (!strcmp(m, "drone.set_camera_zoom"))
        h_drone_set_camera_zoom(srv, client_fd, id, p);
    else if (!strcmp(m, "drone.set_home_location"))
        h_drone_set_home_location(srv, client_fd, id, p);
    else if (!strcmp(m, "drone.set_obstacle_avoidance"))
        h_drone_set_obstacle_avoidance(srv, client_fd, id, p);
    /* actions */
    else if (!strcmp(m, "drone.shoot_photo"))
        h_drone_shoot_photo(srv, client_fd, id);
    else if (!strcmp(m, "drone.start_recording"))
        h_drone_start_recording(srv, client_fd, id);
    else if (!strcmp(m, "drone.stop_recording"))
        h_drone_stop_recording(srv, client_fd, id);
    else {
        log_warn(TAG, "fd=%d unknown method: %s", client_fd, m);
        send_error(srv, client_fd, id, "method not found");
    }

    rpc_request_free(&req);
    return 0;
}
