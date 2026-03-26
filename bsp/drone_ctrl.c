/*
 * drone_ctrl.c — drone control BSP implementation
 *
 * Two build modes:
 *   PSDK_REAL=1  : calls real DJI PSDK APIs
 *   (default)    : stub mode returning mock data for development
 *
 * PSDK data subscription model:
 *   - Subscribe to topics at init (drone_ctrl_init)
 *   - On each get_* call, read the latest cached sample
 *   - Set/action calls invoke the corresponding PSDK command API
 */

#include "drone_ctrl.h"
#include "../core/log/log.h"

#include <string.h>
#include <math.h>
#include <time.h>

#define TAG "bsp.ctrl"

/* ══════════════════════════════════════════════════════════════════════════
 * REAL PSDK implementation
 * ══════════════════════════════════════════════════════════════════════════ */
#ifdef PSDK_REAL

#include <errno.h>
#include <stdio.h>
#include <dji_fc_subscription.h>
#include <dji_gimbal_manager.h>
#include <dji_camera_manager.h>
#include <dji_flight_controller.h>
#include <dji_platform.h>
#include <dji_waypoint_v3.h>

/* ── Subscription callback storage ───────────────────────────────────────── */

static T_DjiFcSubscriptionQuaternion       g_quat;
static T_DjiFcSubscriptionVelocity         g_vel;
static T_DjiFcSubscriptionPositionFused    g_pos_fused;
static T_DjiFcSubscriptionGpsDetails       g_gps;
static T_DjiFcSubscriptionFlightStatus     g_flight_status;
static T_DjiFcSubscriptionWholeBatteryInfo g_battery;
static T_DjiFcSubscriptionSingleBatteryInfo g_battery_single_1;
static T_DjiFcSubscriptionSingleBatteryInfo g_battery_single_2;
static T_DjiFcSubscriptionGimbalAngles     g_gimbal;

typedef struct {
    uint32_t count;
    uint64_t last_update_ms;
    bool logged;
} TopicStats;

static TopicStats g_pos_stats;
static TopicStats g_vel_stats;
static TopicStats g_quat_stats;
static TopicStats g_gps_stats;
static TopicStats g_flight_stats;
static TopicStats g_battery_stats;
static TopicStats g_battery_single_1_stats;
static TopicStats g_battery_single_2_stats;
static TopicStats g_gimbal_stats;
static bool s_waypoint_v3_inited;

static uint64_t monotonic_ms(void) {
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t) ts.tv_sec * 1000ULL + (uint64_t) ts.tv_nsec / 1000000ULL;
}

static void mark_topic_rx(TopicStats *stats, const char *name) {
    stats->count++;
    stats->last_update_ms = monotonic_ms();

    if (!stats->logged) {
        log_info(TAG, "first sample received for %s", name);
        stats->logged = true;
    }
}

static bool topic_is_fresh(const TopicStats *stats, uint64_t max_age_ms) {
    uint64_t now_ms;

    if (stats->last_update_ms == 0) {
        return false;
    }

    now_ms = monotonic_ms();
    return now_ms >= stats->last_update_ms &&
           (now_ms - stats->last_update_ms) <= max_age_ms;
}

static T_DjiReturnCode waypoint_mission_state_cb(T_DjiWaypointV3MissionState missionState) {
    log_info(TAG, "waypoint mission state=%d wayline=%u wp=%u",
             missionState.state, missionState.wayLineId, missionState.currentWaypointIndex);
    return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

static T_DjiReturnCode waypoint_action_state_cb(T_DjiWaypointV3ActionState actionState) {
    log_info(TAG, "waypoint action state=%d wayline=%u wp=%u group=%u action=%u",
             actionState.state, actionState.wayLineId, actionState.currentWaypointIndex,
             actionState.actionGroupId, actionState.actionId);
    return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

static DroneError ensure_waypoint_v3_ready(void) {
    T_DjiReturnCode rc;

    if (s_waypoint_v3_inited)
        return DRONE_OK;

    rc = DjiWaypointV3_Init();
    if (rc != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        log_error(TAG, "DjiWaypointV3_Init failed (0x%08X)", rc);
        return DRONE_ERR_PSDK;
    }

    rc = DjiWaypointV3_RegMissionStateCallback(waypoint_mission_state_cb);
    if (rc != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        log_error(TAG, "DjiWaypointV3_RegMissionStateCallback failed (0x%08X)", rc);
        return DRONE_ERR_PSDK;
    }

    rc = DjiWaypointV3_RegActionStateCallback(waypoint_action_state_cb);
    if (rc != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        log_error(TAG, "DjiWaypointV3_RegActionStateCallback failed (0x%08X)", rc);
        return DRONE_ERR_PSDK;
    }

    s_waypoint_v3_inited = true;
    log_info(TAG, "waypoint v3 module initialised");
    return DRONE_OK;
}

/* ── Subscription callbacks ───────────────────────────────────────────────── */
static T_DjiReturnCode cb_pos(const uint8_t *data, uint16_t len,
                               const T_DjiDataTimestamp *ts) {
    (void)ts;
    if (len >= sizeof(g_pos_fused)) {
        memcpy(&g_pos_fused, data, sizeof(g_pos_fused));
        mark_topic_rx(&g_pos_stats, "POSITION_FUSED");
    }
    return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}
static T_DjiReturnCode cb_vel(const uint8_t *data, uint16_t len,
                               const T_DjiDataTimestamp *ts) {
    (void)ts;
    if (len >= sizeof(g_vel)) {
        memcpy(&g_vel, data, sizeof(g_vel));
        mark_topic_rx(&g_vel_stats, "VELOCITY");
    }
    return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}
static T_DjiReturnCode cb_quat(const uint8_t *data, uint16_t len,
                                const T_DjiDataTimestamp *ts) {
    (void)ts;
    if (len >= sizeof(g_quat)) {
        memcpy(&g_quat, data, sizeof(g_quat));
        mark_topic_rx(&g_quat_stats, "QUATERNION");
    }
    return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}
static T_DjiReturnCode cb_gps(const uint8_t *data, uint16_t len,
                               const T_DjiDataTimestamp *ts) {
    (void)ts;
    if (len >= sizeof(g_gps)) {
        memcpy(&g_gps, data, sizeof(g_gps));
        mark_topic_rx(&g_gps_stats, "GPS_DETAILS");
    }
    return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}
static T_DjiReturnCode cb_flight_status(const uint8_t *data, uint16_t len,
                                         const T_DjiDataTimestamp *ts) {
    (void)ts;
    if (len >= sizeof(g_flight_status)) {
        memcpy(&g_flight_status, data, sizeof(g_flight_status));
        mark_topic_rx(&g_flight_stats, "STATUS_FLIGHT");
    }
    return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}
static T_DjiReturnCode cb_battery(const uint8_t *data, uint16_t len,
                                   const T_DjiDataTimestamp *ts) {
    (void)ts;
    if (len >= sizeof(g_battery)) {
        memcpy(&g_battery, data, sizeof(g_battery));
        mark_topic_rx(&g_battery_stats, "BATTERY_INFO");
    }
    return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}
static T_DjiReturnCode cb_battery_single_1(const uint8_t *data, uint16_t len,
                                           const T_DjiDataTimestamp *ts) {
    (void)ts;
    if (len >= sizeof(g_battery_single_1)) {
        memcpy(&g_battery_single_1, data, sizeof(g_battery_single_1));
        mark_topic_rx(&g_battery_single_1_stats, "BATTERY_SINGLE_INFO_INDEX1");
    }
    return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}
static T_DjiReturnCode cb_battery_single_2(const uint8_t *data, uint16_t len,
                                           const T_DjiDataTimestamp *ts) {
    (void)ts;
    if (len >= sizeof(g_battery_single_2)) {
        memcpy(&g_battery_single_2, data, sizeof(g_battery_single_2));
        mark_topic_rx(&g_battery_single_2_stats, "BATTERY_SINGLE_INFO_INDEX2");
    }
    return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}
static T_DjiReturnCode cb_gimbal(const uint8_t *data, uint16_t len,
                                  const T_DjiDataTimestamp *ts) {
    (void)ts;
    if (len >= sizeof(g_gimbal)) {
        memcpy(&g_gimbal, data, sizeof(g_gimbal));
        mark_topic_rx(&g_gimbal_stats, "GIMBAL_ANGLES");
    }
    return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

/* ── Init / deinit ────────────────────────────────────────────────────────── */
int drone_ctrl_init(void) {
    T_DjiReturnCode rc;
    bool battery_topic_ok = false;
    bool gimbal_topic_ok = false;

    rc = DjiFcSubscription_Init();
    if (rc != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        log_error(TAG, "DjiFcSubscription_Init failed (0x%08X)", rc);
        return -1;
    }

#define SUBSCRIBE(topic, cb, freq) do { \
    rc = DjiFcSubscription_SubscribeTopic(topic, freq, cb); \
    if (rc != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) \
        log_warn(TAG, "subscribe " #topic " failed (0x%08X)", rc); \
} while (0)

    SUBSCRIBE(DJI_FC_SUBSCRIPTION_TOPIC_POSITION_FUSED,
              cb_pos, DJI_DATA_SUBSCRIPTION_TOPIC_10_HZ);
    SUBSCRIBE(DJI_FC_SUBSCRIPTION_TOPIC_VELOCITY,
              cb_vel, DJI_DATA_SUBSCRIPTION_TOPIC_10_HZ);
    SUBSCRIBE(DJI_FC_SUBSCRIPTION_TOPIC_QUATERNION,
              cb_quat, DJI_DATA_SUBSCRIPTION_TOPIC_10_HZ);
    SUBSCRIBE(DJI_FC_SUBSCRIPTION_TOPIC_GPS_DETAILS,
              cb_gps, DJI_DATA_SUBSCRIPTION_TOPIC_1_HZ);
    SUBSCRIBE(DJI_FC_SUBSCRIPTION_TOPIC_STATUS_FLIGHT,
              cb_flight_status, DJI_DATA_SUBSCRIPTION_TOPIC_1_HZ);
    rc = DjiFcSubscription_SubscribeTopic(DJI_FC_SUBSCRIPTION_TOPIC_BATTERY_INFO,
                                          DJI_DATA_SUBSCRIPTION_TOPIC_1_HZ,
                                          cb_battery);
    if (rc != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        log_warn(TAG, "subscribe DJI_FC_SUBSCRIPTION_TOPIC_BATTERY_INFO failed (0x%08X)", rc);
    } else {
        battery_topic_ok = true;
    }
    SUBSCRIBE(DJI_FC_SUBSCRIPTION_TOPIC_BATTERY_SINGLE_INFO_INDEX1,
              cb_battery_single_1, DJI_DATA_SUBSCRIPTION_TOPIC_1_HZ);
    SUBSCRIBE(DJI_FC_SUBSCRIPTION_TOPIC_BATTERY_SINGLE_INFO_INDEX2,
              cb_battery_single_2, DJI_DATA_SUBSCRIPTION_TOPIC_1_HZ);
    rc = DjiFcSubscription_SubscribeTopic(DJI_FC_SUBSCRIPTION_TOPIC_GIMBAL_ANGLES,
                                          DJI_DATA_SUBSCRIPTION_TOPIC_10_HZ,
                                          cb_gimbal);
    if (rc != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        log_warn(TAG, "subscribe DJI_FC_SUBSCRIPTION_TOPIC_GIMBAL_ANGLES failed (0x%08X)", rc);
    } else {
        gimbal_topic_ok = true;
    }
#undef SUBSCRIBE

    if (!battery_topic_ok) {
        log_info(TAG, "battery telemetry will fall back to single-battery topics when whole-pack topic is unavailable");
    }

    if (!gimbal_topic_ok) {
        log_info(TAG, "gimbal angle topic may be unavailable on this aircraft; getter will return cached/default values");
    }

    rc = DjiCameraManager_Init();
    if (rc != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
        log_warn(TAG, "DjiCameraManager_Init failed (non-fatal)");

    rc = DjiGimbalManager_Init();
    if (rc != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
        log_warn(TAG, "DjiGimbalManager_Init failed (non-fatal)");

    log_info(TAG, "drone_ctrl initialised (PSDK subscriptions active)");
    return 0;
}

void drone_ctrl_deinit(void) {
    if (s_waypoint_v3_inited) {
        T_DjiReturnCode rc = DjiWaypointV3_DeInit();
        if (rc != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
            log_warn(TAG, "DjiWaypointV3_DeInit failed (0x%08X)", rc);
        s_waypoint_v3_inited = false;
    }
    DjiGimbalManager_Deinit();
    DjiCameraManager_DeInit();
    DjiFcSubscription_DeInit();
    log_info(TAG, "drone_ctrl de-initialised");
}

/* ── Helpers: quaternion → heading ───────────────────────────────────────── */
static float quat_to_heading(const T_DjiFcSubscriptionQuaternion *q) {
    /* yaw from quaternion: atan2(2*(w*z + x*y), 1 - 2*(y*y + z*z)) */
    float yaw = atan2f(2.0f * (q->q0 * q->q3 + q->q1 * q->q2),
                       1.0f - 2.0f * (q->q2 * q->q2 + q->q3 * q->q3));
    float deg = yaw * 180.0f / 3.14159265f;
    return deg < 0 ? deg + 360.0f : deg;
}

/* ── GET implementations ──────────────────────────────────────────────────── */

DroneError drone_get_telemetry(DroneTelemetry *out) {
    uint32_t sats_from_pos = g_pos_fused.visibleSatelliteNumber;
    uint32_t sats_from_gps = g_gps.gpsSatelliteNumberUsed;

    memset(out, 0, sizeof(*out));
    out->lat         = g_pos_fused.latitude  * 180.0 / 3.14159265358979;
    out->lon         = g_pos_fused.longitude * 180.0 / 3.14159265358979;
    out->alt_msl_m   = g_pos_fused.altitude;
    out->alt_rel_m   = g_pos_fused.altitude; /* fused altitude, WGS84 */
    out->vx_ms       = g_vel.data.x;
    out->vy_ms       = g_vel.data.y;
    out->vz_ms       = g_vel.data.z;
    out->heading_deg = quat_to_heading(&g_quat);
    out->battery_pct = g_battery.percentage;
    out->battery_mv  = (uint32_t)g_battery.voltage;
    out->gps_sats    = sats_from_gps > sats_from_pos ? sats_from_gps : sats_from_pos;
    out->gps_fix     = (uint8_t)g_gps.fixState;
    out->flight_status = g_flight_status;
    out->motors_on   = (g_flight_status > 0);

    if (!topic_is_fresh(&g_battery_stats, 5000)) {
        if (g_battery_single_1_stats.last_update_ms || g_battery_single_2_stats.last_update_ms) {
            uint32_t pct_sum = 0;
            uint32_t pct_count = 0;
            int32_t current_sum = 0;
            uint32_t voltage_mv = 0;

            if (g_battery_single_1_stats.last_update_ms) {
                pct_sum += g_battery_single_1.batteryCapacityPercent;
                pct_count++;
                current_sum += g_battery_single_1.currentElectric;
                if ((uint32_t) g_battery_single_1.currentVoltage > voltage_mv)
                    voltage_mv = (uint32_t) g_battery_single_1.currentVoltage;
            }
            if (g_battery_single_2_stats.last_update_ms) {
                pct_sum += g_battery_single_2.batteryCapacityPercent;
                pct_count++;
                current_sum += g_battery_single_2.currentElectric;
                if ((uint32_t) g_battery_single_2.currentVoltage > voltage_mv)
                    voltage_mv = (uint32_t) g_battery_single_2.currentVoltage;
            }

            if (pct_count > 0)
                out->battery_pct = (uint8_t) (pct_sum / pct_count);
            out->battery_mv = voltage_mv;
            (void) current_sum;
        }
    }

    return DRONE_OK;
}

DroneError drone_get_battery_info(DroneBatteryInfo *out) {
    memset(out, 0, sizeof(*out));
    out->voltage_mv    = (uint32_t)g_battery.voltage;
    out->current_ma    = g_battery.current;
    out->remaining_pct = g_battery.percentage;
    out->temperature_dc = 0; /* query DjiBattery_GetInfo if needed */
    out->cycle_count   = 0;

    if (!topic_is_fresh(&g_battery_stats, 5000)) {
        uint32_t pct_sum = 0;
        uint32_t pct_count = 0;

        if (g_battery_single_1_stats.last_update_ms) {
            out->voltage_mv = (uint32_t) g_battery_single_1.currentVoltage;
            out->current_ma += g_battery_single_1.currentElectric;
            out->temperature_dc = g_battery_single_1.batteryTemperature;
            pct_sum += g_battery_single_1.batteryCapacityPercent;
            pct_count++;
        }

        if (g_battery_single_2_stats.last_update_ms) {
            uint32_t voltage_mv = (uint32_t) g_battery_single_2.currentVoltage;

            if (voltage_mv > out->voltage_mv)
                out->voltage_mv = voltage_mv;
            out->current_ma += g_battery_single_2.currentElectric;
            if (g_battery_single_2.batteryTemperature > out->temperature_dc)
                out->temperature_dc = g_battery_single_2.batteryTemperature;
            pct_sum += g_battery_single_2.batteryCapacityPercent;
            pct_count++;
        }

        if (pct_count > 0)
            out->remaining_pct = (uint8_t) (pct_sum / pct_count);
    }

    return DRONE_OK;
}

DroneError drone_get_gimbal_angle(DroneGimbalAngle *out) {
    /* g_gimbal is T_DjiFcSubscriptionGimbalAngles = T_DjiVector3f (x=pitch, y=roll, z=yaw) */
    out->pitch = g_gimbal.x;
    out->roll  = g_gimbal.y;
    out->yaw   = g_gimbal.z;
    return DRONE_OK;
}

DroneError drone_get_camera_state(DroneCameraState *out) {
    E_DjiCameraManagerWorkMode mode;
    E_DjiCameraManagerRecordingState recordingState;
    T_DjiCameraManagerOpticalZoomParam opticalZoom = {0};
    T_DjiReturnCode rc = DjiCameraManager_GetMode(
        DJI_MOUNT_POSITION_PAYLOAD_PORT_NO1, &mode);
    if (rc != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) return DRONE_ERR_PSDK;
    out->mode        = (mode == DJI_CAMERA_MANAGER_WORK_MODE_SHOOT_PHOTO)
                       ? CAMERA_MODE_PHOTO : CAMERA_MODE_VIDEO;

    rc = DjiCameraManager_GetRecordingState(DJI_MOUNT_POSITION_PAYLOAD_PORT_NO1,
                                            &recordingState);
    out->is_recording = (rc == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS &&
                         recordingState == DJI_CAMERA_MANAGER_RECORDING_STATE_RECORDING);

    rc = DjiCameraManager_GetOpticalZoomParam(DJI_MOUNT_POSITION_PAYLOAD_PORT_NO1,
                                              &opticalZoom);
    out->zoom_factor  = (rc == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS &&
                         opticalZoom.currentOpticalZoomFactor > 0.0f)
                        ? opticalZoom.currentOpticalZoomFactor
                        : 1.0f;

    return DRONE_OK;
}

DroneError drone_get_rtk_status(DroneRtkStatus *out) {
    memset(out, 0, sizeof(*out));
    /* RTK data via fc subscription if available */
    out->enabled  = false;
    out->fix_type = 0;
    return DRONE_OK;
}

/* ── SET implementations ──────────────────────────────────────────────────── */

DroneError drone_set_rth_altitude(float alt_m) {
    T_DjiReturnCode rc = DjiFlightController_SetGoHomeAltitude(
        (uint16_t)alt_m);
    return rc == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS ? DRONE_OK : DRONE_ERR_PSDK;
}

DroneError drone_set_gimbal_angle(const DroneGimbalAngle *a) {
    T_DjiGimbalManagerRotation rot = {
        .rotationMode = DJI_GIMBAL_ROTATION_MODE_ABSOLUTE_ANGLE,
        .pitch = a->pitch,
        .roll  = a->roll,
        .yaw   = a->yaw,
        .time  = 0.5f,  /* 0.5 s to reach target */
    };
    T_DjiReturnCode rc = DjiGimbalManager_Rotate(
        DJI_MOUNT_POSITION_PAYLOAD_PORT_NO1, rot);
    return rc == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS ? DRONE_OK : DRONE_ERR_PSDK;
}

DroneError drone_set_camera_mode(CameraMode mode) {
    E_DjiCameraManagerWorkMode psdk_mode =
        (mode == CAMERA_MODE_PHOTO)
        ? DJI_CAMERA_MANAGER_WORK_MODE_SHOOT_PHOTO
        : DJI_CAMERA_MANAGER_WORK_MODE_RECORD_VIDEO;
    T_DjiReturnCode rc = DjiCameraManager_SetMode(
        DJI_MOUNT_POSITION_PAYLOAD_PORT_NO1, psdk_mode);
    return rc == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS ? DRONE_OK : DRONE_ERR_PSDK;
}

DroneError drone_set_camera_zoom(float zoom_factor) {
    E_DjiCameraZoomDirection dir = (zoom_factor >= 1.0f)
        ? DJI_CAMERA_ZOOM_DIRECTION_IN : DJI_CAMERA_ZOOM_DIRECTION_OUT;
    T_DjiReturnCode rc = DjiCameraManager_SetOpticalZoomParam(
        DJI_MOUNT_POSITION_PAYLOAD_PORT_NO1, dir, zoom_factor);
    return rc == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS ? DRONE_OK : DRONE_ERR_PSDK;
}

DroneError drone_set_home_location(const DroneHomeLocation *loc) {
    T_DjiFlightControllerHomeLocation home = {
        .latitude  = loc->lat,
        .longitude = loc->lon,
    };
    T_DjiReturnCode rc = DjiFlightController_SetHomeLocationUsingGPSCoordinates(
        home);
    return rc == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS ? DRONE_OK : DRONE_ERR_PSDK;
}

DroneError drone_set_obstacle_avoidance(ObstacleAvoidMode mode) {
    E_DjiFlightControllerObstacleAvoidanceEnableStatus sw =
        (mode == OBSTACLE_AVOID_CLOSE)
        ? DJI_FLIGHT_CONTROLLER_ENABLE_OBSTACLE_AVOIDANCE
        : DJI_FLIGHT_CONTROLLER_DISABLE_OBSTACLE_AVOIDANCE;
    /* horizontal */
    T_DjiReturnCode rc =
        DjiFlightController_SetHorizontalVisualObstacleAvoidanceEnableStatus(sw);
    if (rc != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) return DRONE_ERR_PSDK;
    /* upward */
    rc = DjiFlightController_SetUpwardsVisualObstacleAvoidanceEnableStatus(sw);
    if (rc != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) return DRONE_ERR_PSDK;
    return DRONE_OK;
}

DroneError drone_shoot_photo(void) {
    T_DjiReturnCode rc = DjiCameraManager_StartShootPhoto(
        DJI_MOUNT_POSITION_PAYLOAD_PORT_NO1,
        DJI_CAMERA_MANAGER_SHOOT_PHOTO_MODE_SINGLE);
    return rc == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS ? DRONE_OK : DRONE_ERR_PSDK;
}

DroneError drone_start_recording(void) {
    T_DjiReturnCode rc = DjiCameraManager_StartRecordVideo(
        DJI_MOUNT_POSITION_PAYLOAD_PORT_NO1);
    return rc == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS ? DRONE_OK : DRONE_ERR_PSDK;
}

DroneError drone_stop_recording(void) {
    T_DjiReturnCode rc = DjiCameraManager_StopRecordVideo(
        DJI_MOUNT_POSITION_PAYLOAD_PORT_NO1);
    return rc == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS ? DRONE_OK : DRONE_ERR_PSDK;
}

DroneError drone_upload_kmz(const char *path) {
    FILE *fp = NULL;
    long file_size;
    size_t read_len;
    uint8_t *buf = NULL;
    T_DjiOsalHandler *osal;
    T_DjiReturnCode rc;
    DroneError err;

    if (path == NULL || path[0] == '\0')
        return DRONE_ERR_PARAM;

    err = ensure_waypoint_v3_ready();
    if (err != DRONE_OK)
        return err;

    fp = fopen(path, "rb");
    if (fp == NULL) {
        log_error(TAG, "open kmz failed: %s (%s)", path, strerror(errno));
        return DRONE_ERR_PARAM;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return DRONE_ERR_UNKNOWN;
    }

    file_size = ftell(fp);
    if (file_size <= 0) {
        fclose(fp);
        return DRONE_ERR_PARAM;
    }

    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return DRONE_ERR_UNKNOWN;
    }

    osal = DjiPlatform_GetOsalHandler();
    if (osal == NULL) {
        fclose(fp);
        return DRONE_ERR_PSDK;
    }

    buf = osal->Malloc((uint32_t) file_size);
    if (buf == NULL) {
        fclose(fp);
        return DRONE_ERR_UNKNOWN;
    }

    read_len = fread(buf, 1, (size_t) file_size, fp);
    fclose(fp);
    if (read_len != (size_t) file_size) {
        osal->Free(buf);
        return DRONE_ERR_UNKNOWN;
    }

    log_info(TAG, "uploading kmz: %s (%ld bytes)", path, file_size);
    rc = DjiWaypointV3_UploadKmzFile(buf, (uint32_t) file_size);
    osal->Free(buf);

    if (rc != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        log_error(TAG, "DjiWaypointV3_UploadKmzFile failed (0x%08X)", rc);
        return DRONE_ERR_PSDK;
    }

    log_info(TAG, "kmz uploaded successfully: %s", path);
    return DRONE_OK;
}

/* ══════════════════════════════════════════════════════════════════════════
 * STUB implementation (development without PSDK hardware)
 * ══════════════════════════════════════════════════════════════════════════ */
#else

#include <time.h>

static float g_stub_rth_alt    = 30.0f;
static float g_stub_gimbal_p   = -30.0f;
static float g_stub_zoom       = 1.0f;
static int   g_stub_cam_mode   = CAMERA_MODE_PHOTO;
static bool  g_stub_recording  = false;

int drone_ctrl_init(void) {
    log_info(TAG, "drone_ctrl stub init (no PSDK hardware)");
    return 0;
}

void drone_ctrl_deinit(void) {
    log_info(TAG, "drone_ctrl stub deinit");
}

DroneError drone_get_telemetry(DroneTelemetry *out) {
    /* Return slowly varying mock position for UI testing */
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    double t = ts.tv_sec + ts.tv_nsec * 1e-9;

    memset(out, 0, sizeof(*out));
    out->lat           = 22.5430 + 0.0001 * sin(t * 0.1);
    out->lon           = 113.9310 + 0.0001 * cos(t * 0.1);
    out->alt_msl_m     = 50.0f + 5.0f * (float)sin(t * 0.05);
    out->alt_rel_m     = 30.0f;
    out->vx_ms         = 1.5f;
    out->vy_ms         = 0.3f;
    out->vz_ms         = 0.0f;
    out->heading_deg   = (float)fmod(t * 5.0, 360.0);
    out->battery_pct   = 82;
    out->battery_mv    = 25200;
    out->gps_sats      = 14;
    out->gps_fix       = 2;
    out->flight_status = 2; /* in_air */
    out->motors_on     = true;
    return DRONE_OK;
}

DroneError drone_get_battery_info(DroneBatteryInfo *out) {
    memset(out, 0, sizeof(*out));
    out->voltage_mv    = 25200;
    out->current_ma    = -3200;
    out->remaining_pct = 82;
    out->temperature_dc = 253;  /* 25.3 °C */
    out->cycle_count   = 47;
    return DRONE_OK;
}

DroneError drone_get_gimbal_angle(DroneGimbalAngle *out) {
    out->pitch = g_stub_gimbal_p;
    out->roll  = 0.0f;
    out->yaw   = 0.0f;
    return DRONE_OK;
}

DroneError drone_get_camera_state(DroneCameraState *out) {
    out->mode         = (CameraMode)g_stub_cam_mode;
    out->is_recording = g_stub_recording;
    out->zoom_factor  = g_stub_zoom;
    return DRONE_OK;
}

DroneError drone_get_rtk_status(DroneRtkStatus *out) {
    memset(out, 0, sizeof(*out));
    out->enabled    = false;
    out->fix_type   = 0;
    out->satellites = 0;
    return DRONE_OK;
}

DroneError drone_set_rth_altitude(float alt_m) {
    log_info(TAG, "stub: set RTH altitude %.1f m", alt_m);
    g_stub_rth_alt = alt_m;
    return DRONE_OK;
}

DroneError drone_set_gimbal_angle(const DroneGimbalAngle *a) {
    log_info(TAG, "stub: set gimbal pitch=%.1f roll=%.1f yaw=%.1f",
             a->pitch, a->roll, a->yaw);
    g_stub_gimbal_p = a->pitch;
    return DRONE_OK;
}

DroneError drone_set_camera_mode(CameraMode mode) {
    log_info(TAG, "stub: set camera mode %d", mode);
    g_stub_cam_mode = mode;
    return DRONE_OK;
}

DroneError drone_set_camera_zoom(float zoom_factor) {
    log_info(TAG, "stub: set camera zoom %.2f", zoom_factor);
    g_stub_zoom = zoom_factor;
    return DRONE_OK;
}

DroneError drone_set_home_location(const DroneHomeLocation *loc) {
    log_info(TAG, "stub: set home %.6f,%.6f alt=%.1f",
             loc->lat, loc->lon, loc->alt_m);
    return DRONE_OK;
}

DroneError drone_set_obstacle_avoidance(ObstacleAvoidMode mode) {
    log_info(TAG, "stub: set obstacle avoidance mode %d", mode);
    return DRONE_OK;
}

DroneError drone_shoot_photo(void) {
    log_info(TAG, "stub: shoot photo");
    return DRONE_OK;
}

DroneError drone_start_recording(void) {
    log_info(TAG, "stub: start recording");
    g_stub_recording = true;
    return DRONE_OK;
}

DroneError drone_stop_recording(void) {
    log_info(TAG, "stub: stop recording");
    g_stub_recording = false;
    return DRONE_OK;
}

DroneError drone_upload_kmz(const char *path) {
    if (path == NULL || path[0] == '\0')
        return DRONE_ERR_PARAM;
    log_info(TAG, "stub: upload kmz %s", path);
    return DRONE_OK;
}

#endif /* PSDK_REAL */
