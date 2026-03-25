#ifndef DRONE_CTRL_H
#define DRONE_CTRL_H

#include "../include/drone_types.h"

/*
 * drone_ctrl.h — drone control API (BSP layer)
 *
 * All functions are synchronous from the caller's perspective.
 * Internally they call PSDK APIs which may block briefly.
 *
 * Returns DRONE_OK on success, negative DroneError on failure.
 *
 * In stub mode (no PSDK_REAL), read functions return plausible mock data
 * and set functions succeed silently — allowing the full server stack to
 * be developed and tested without physical hardware.
 */

/* ── Initialise / deinitialise PSDK data subscriptions ───────────────────── */
int  drone_ctrl_init(void);
void drone_ctrl_deinit(void);

/* ── Read (GET) ───────────────────────────────────────────────────────────── */
DroneError drone_get_telemetry    (DroneTelemetry   *out);
DroneError drone_get_battery_info (DroneBatteryInfo *out);
DroneError drone_get_gimbal_angle (DroneGimbalAngle *out);
DroneError drone_get_camera_state (DroneCameraState *out);
DroneError drone_get_rtk_status   (DroneRtkStatus   *out);

/* ── Write (SET) ──────────────────────────────────────────────────────────── */
DroneError drone_set_rth_altitude       (float alt_m);
DroneError drone_set_gimbal_angle       (const DroneGimbalAngle *angle);
DroneError drone_set_camera_mode        (CameraMode mode);
DroneError drone_set_camera_zoom        (float zoom_factor);
DroneError drone_set_home_location      (const DroneHomeLocation *loc);
DroneError drone_set_obstacle_avoidance (ObstacleAvoidMode mode);

/* ── Actions ──────────────────────────────────────────────────────────────── */
DroneError drone_shoot_photo      (void);
DroneError drone_start_recording  (void);
DroneError drone_stop_recording   (void);

/* ── Wayline / KMZ ───────────────────────────────────────────────────────── */
DroneError drone_upload_kmz       (const char *path);

#endif /* DRONE_CTRL_H */
