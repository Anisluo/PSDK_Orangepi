#ifndef DRONE_TYPES_H
#define DRONE_TYPES_H

#include <stdint.h>
#include <stdbool.h>

/*
 * drone_types.h — shared data types for the PSDK bridge daemon
 *
 * Protocol: JSON-RPC over TCP, newline (\n) delimited
 * Port    : DRONE_SERVER_PORT (default 5555, configurable via --port)
 *
 * Method naming mirrors Protocol.h in HostGUI:
 *   drone.get_telemetry   drone.get_battery_info  drone.get_gimbal_angle
 *   drone.get_camera_state drone.get_rtk_status
 *   drone.set_rth_altitude drone.set_gimbal_angle  drone.set_camera_mode
 *   drone.set_camera_zoom  drone.set_home_location drone.set_obstacle_avoidance
 *   drone.shoot_photo      drone.start_recording   drone.stop_recording
 *   system.ping
 */

/* ── Server config ────────────────────────────────────────────────────────── */
#define DRONE_SERVER_PORT    5555
#define DRONE_SERVER_BIND_IP "192.168.1.102"   /* default mesh interface IP */
#define DRONE_RPC_BACKLOG    4
#define DRONE_MAX_CLIENTS   8
#define DRONE_MAX_MSG_LEN   4096   /* max single JSON-RPC message (bytes) */
#define DRONE_RECV_BUF_LEN  8192   /* per-client receive ring buffer */

/* ── Error codes ──────────────────────────────────────────────────────────── */
typedef enum {
    DRONE_OK            =  0,
    DRONE_ERR_PSDK      = -1,   /* PSDK API returned failure */
    DRONE_ERR_PARAM     = -2,   /* invalid or missing parameter */
    DRONE_ERR_STATE     = -3,   /* drone in wrong state for this command */
    DRONE_ERR_TIMEOUT   = -4,   /* PSDK call timed out */
    DRONE_ERR_UNKNOWN   = -99,
} DroneError;

/* ── Telemetry ────────────────────────────────────────────────────────────── */
typedef struct {
    double   lat;           /* degrees, WGS-84 */
    double   lon;           /* degrees, WGS-84 */
    float    alt_msl_m;     /* altitude above sea level, m */
    float    alt_rel_m;     /* altitude relative to home point, m */
    float    vx_ms;         /* velocity north (m/s) */
    float    vy_ms;         /* velocity east (m/s) */
    float    vz_ms;         /* velocity down (m/s, positive = descending) */
    float    heading_deg;   /* 0–360 degrees, clockwise from north */
    uint8_t  battery_pct;   /* remaining capacity 0–100 */
    uint32_t battery_mv;    /* battery voltage, millivolts */
    uint8_t  gps_sats;      /* visible satellites */
    uint8_t  gps_fix;       /* 0=none 1=2D 2=3D 3=RTK-float 4=RTK-fixed */
    uint8_t  flight_status; /* 0=on_ground 1=taking_off 2=in_air 3=landing */
    bool     motors_on;
} DroneTelemetry;

/* ── Gimbal ───────────────────────────────────────────────────────────────── */
typedef struct {
    float pitch;   /* degrees, negative = nose down */
    float roll;    /* degrees */
    float yaw;     /* degrees relative to aircraft heading */
} DroneGimbalAngle;

/* ── Camera ───────────────────────────────────────────────────────────────── */
typedef enum {
    CAMERA_MODE_PHOTO = 0,
    CAMERA_MODE_VIDEO = 1,
} CameraMode;

typedef struct {
    CameraMode mode;
    bool       is_recording;
    float      zoom_factor;    /* 1.0 = no zoom */
} DroneCameraState;

/* ── Battery ──────────────────────────────────────────────────────────────── */
typedef struct {
    uint32_t voltage_mv;       /* millivolts */
    int32_t  current_ma;       /* milliamps, negative = discharging */
    uint8_t  remaining_pct;    /* 0–100 */
    int16_t  temperature_dc;   /* tenths of celsius, e.g. 253 = 25.3 °C */
    uint32_t cycle_count;
} DroneBatteryInfo;

/* ── RTK status ───────────────────────────────────────────────────────────── */
typedef struct {
    bool    enabled;
    uint8_t fix_type;    /* 0=none 1=single 2=float 3=fixed */
    uint8_t satellites;
    double  lat;
    double  lon;
    float   alt_m;
} DroneRtkStatus;

/* ── Home location ────────────────────────────────────────────────────────── */
typedef struct {
    double lat;
    double lon;
    float  alt_m;
} DroneHomeLocation;

/* ── Obstacle avoidance mode ──────────────────────────────────────────────── */
typedef enum {
    OBSTACLE_AVOID_CLOSE  = 0,  /* disabled */
    OBSTACLE_AVOID_OPEN   = 1,  /* enabled */
    OBSTACLE_AVOID_BRAKE  = 2,  /* brake on obstacle */
} ObstacleAvoidMode;

#endif /* DRONE_TYPES_H */
