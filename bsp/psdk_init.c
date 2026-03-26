/*
 * psdk_init.c — PSDK 3.9.2 initialisation and shutdown
 *
 * Call order (PSDK_REAL=1):
 *   psdk_hal_register()        — registers OSAL, UART, Network, Socket, FS
 *   DjiCore_Init()             — initialises SDK with app credentials
 *   DjiCore_SetAlias()         — payload alias shown in DJI Pilot
 *   DjiCore_SetFirmwareVersion()
 *   DjiCore_SetSerialNumber()
 *   DjiCore_ApplicationStart() — completes handshake with aircraft
 *
 * App credentials go in bsp/dji_sdk_app_info.h (not committed to git).
 * Get your credentials at https://developer.dji.com/user/apps/
 */

#include "psdk_init.h"
#include "psdk_hal.h"
#include "../core/log/log.h"

#define TAG "bsp.init"

/* ══════════════════════════════════════════════════════════════════════════
 * REAL PSDK init
 * ══════════════════════════════════════════════════════════════════════════ */
#ifdef PSDK_REAL

#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <dji_core.h>
#include "dji_sdk_app_info.h"   /* fill in your credentials here */

static const char *psdk_init_rc_name(T_DjiReturnCode rc) {
    switch (rc) {
        case DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS:
            return "success";
        case 0x000000E1:
            return "payload_negotiate_timeout";
        case 0x000000E3:
            return "app_auth_verify_failed";
        case 0x000000FF:
            return "payload_remove_device_sync_error";
        default:
            return "unknown";
    }
}

static int psdk_init_max_attempts_default(void) {
#ifdef DRONE_MODEL_M3E
    return 0;
#else
    return 30;
#endif
}

static int psdk_init_max_attempts_from_env(void) {
    const char *value = getenv("PSDK_INIT_MAX_ATTEMPTS");
    char *end = NULL;
    long parsed;

    if (value == NULL || *value == '\0')
        return psdk_init_max_attempts_default();

    parsed = strtol(value, &end, 10);
    if (end == value || *end != '\0') {
        log_warn(TAG, "invalid PSDK_INIT_MAX_ATTEMPTS=%s, using default", value);
        return psdk_init_max_attempts_default();
    }

    if (parsed < 0)
        parsed = 0;
    if (parsed > 100000)
        parsed = 100000;
    return (int)parsed;
}

int psdk_init(void) {
    /* Step 1: register platform handlers */
    if (psdk_hal_register() != 0) return -1;

    /* In M3TD mode, keep FunctionFS bulk endpoints open before DjiCore_Init().
     * This gives the aircraft a live USB Bulk receive path during negotiate. */
    if (psdk_hal_usb_bulk_prepare() != 0) {
        log_error(TAG, "USB bulk pre-open failed");
        return -1;
    }

    /* Step 2: initialise core with app info */
    T_DjiUserInfo user_info;
    memset(&user_info, 0, sizeof(user_info));
    strncpy(user_info.appName,         USER_APP_NAME,         sizeof(user_info.appName) - 1);
    strncpy(user_info.appId,           USER_APP_ID,           sizeof(user_info.appId) - 1);
    strncpy(user_info.appKey,          USER_APP_KEY,          sizeof(user_info.appKey) - 1);
    strncpy(user_info.appLicense,      USER_APP_LICENSE,      sizeof(user_info.appLicense) - 1);
    strncpy(user_info.developerAccount,USER_DEVELOPER_ACCOUNT,sizeof(user_info.developerAccount) - 1);
    strncpy(user_info.baudRate,        USER_BAUD_RATE,        sizeof(user_info.baudRate) - 1);

    /* DjiCore_Init may fail the first few attempts while the aircraft's
     * E-Port UART is still booting. Retry internally so psdkd keeps the
     * UART open and sends identification packets — this prevents E-Port
     * from triggering its power-cut timeout between retries. */
    T_DjiReturnCode rc;
    int attempt = 0;
    int max_init_attempts = psdk_init_max_attempts_from_env();
    struct timespec init_started = {0};
    struct timespec init_finished = {0};

    if (max_init_attempts == 0) {
        log_info(TAG, "DjiCore_Init retry policy: unlimited attempts (keep link active)");
    } else {
        log_info(TAG, "DjiCore_Init retry policy: max %d attempts", max_init_attempts);
    }

    do {
        if (attempt > 0) {
            log_warn(TAG,
                     "DjiCore_Init attempt %d failed (%s, 0x%08X), retrying in 1s...",
                     attempt, psdk_init_rc_name(rc), rc);
            DjiCore_DeInit();
            sleep(1);
        }
        clock_gettime(CLOCK_MONOTONIC, &init_started);
        rc = DjiCore_Init(&user_info);
        clock_gettime(CLOCK_MONOTONIC, &init_finished);
        attempt++;
        log_info(TAG,
                 "DjiCore_Init attempt %d finished in %lld ms with %s (0x%08X)",
                 attempt,
                 (long long)((init_finished.tv_sec - init_started.tv_sec) * 1000LL +
                             (init_finished.tv_nsec - init_started.tv_nsec) / 1000000LL),
                 psdk_init_rc_name(rc), rc);
    } while (rc != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS &&
             (max_init_attempts == 0 || attempt < max_init_attempts));

    if (rc != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        if (max_init_attempts == 0) {
            log_error(TAG, "DjiCore_Init stopped after %d attempts (%s, 0x%08X)",
                      attempt, psdk_init_rc_name(rc), rc);
        } else {
            log_error(TAG, "DjiCore_Init failed after %d/%d attempts (%s, 0x%08X)",
                      attempt, max_init_attempts, psdk_init_rc_name(rc), rc);
        }
        psdk_hal_usb_bulk_release();
        return -1;
    }
    log_info(TAG, "DjiCore_Init OK (attempt %d)", attempt);

    /* Step 3: optional metadata */
    rc = DjiCore_SetAlias("PSDK-Bridge");
    if (rc != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
        log_warn(TAG, "DjiCore_SetAlias failed (non-fatal, 0x%08X)", rc);

    T_DjiFirmwareVersion fw = { .majorVersion = 1, .minorVersion = 0,
                                 .modifyVersion = 0, .debugVersion = 0 };
    rc = DjiCore_SetFirmwareVersion(fw);
    if (rc != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
        log_warn(TAG, "DjiCore_SetFirmwareVersion failed (non-fatal, 0x%08X)", rc);

    rc = DjiCore_SetSerialNumber("OPSDK00000001");
    if (rc != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
        log_warn(TAG, "DjiCore_SetSerialNumber failed (non-fatal, 0x%08X)", rc);

    /* Step 4: start application — completes E-Port handshake */
    rc = DjiCore_ApplicationStart();
    if (rc != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        log_error(TAG, "DjiCore_ApplicationStart failed (0x%08X)", rc);
        psdk_hal_usb_bulk_release();
        return -1;
    }
    log_info(TAG, "PSDK application started — E-Port handshake complete");
    return 0;
}

void psdk_deinit(void) {
    DjiCore_DeInit();
    psdk_hal_usb_bulk_release();
    log_info(TAG, "PSDK de-initialised");
}

/* ══════════════════════════════════════════════════════════════════════════
 * STUB (no PSDK hardware)
 * ══════════════════════════════════════════════════════════════════════════ */
#else

int psdk_init(void) {
    psdk_hal_register();
    log_info(TAG, "PSDK stub init OK (build with PSDK_REAL=1 for real hardware)");
    return 0;
}

void psdk_deinit(void) {
    log_info(TAG, "PSDK stub deinit");
}

#endif /* PSDK_REAL */
