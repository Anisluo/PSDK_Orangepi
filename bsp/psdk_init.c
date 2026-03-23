/*
 * psdk_init.c — PSDK initialisation and shutdown
 *
 * PSDK DjiCore_Init() requires:
 *   1. HAL handlers registered (psdk_hal_register)
 *   2. App info: developer account hash, app ID, app key, app license,
 *      firmware version, alias name — all from DJI Developer Portal.
 *      Fill these in dji_sdk_app_info.h (not committed to git).
 *
 * Reference: PSDK samples/sample_c/module_sample/utils/dji_sdk_app_info.h
 */

#include "psdk_init.h"
#include "psdk_hal.h"
#include "../core/log/log.h"

#define TAG "bsp.init"

#ifdef PSDK_REAL
/* ─────────────────────────────────────────────────────────────────────────
 * Real PSDK init
 * ───────────────────────────────────────────────────────────────────────── */

#include <dji_core.h>
#include <dji_logger.h>
#include "dji_sdk_app_info.h"   /* developer-specific, not in repo */

static T_DjiReturnCode psdk_log_callback(const uint8_t *data, uint16_t data_len) {
    /* Route PSDK internal log to our logger */
    char buf[512];
    size_t copy = data_len < sizeof(buf) - 1 ? data_len : sizeof(buf) - 1;
    memcpy(buf, data, copy);
    buf[copy] = '\0';
    log_debug("bsp.psdk", "%s", buf);
    return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

int psdk_init(void) {
    if (psdk_hal_register() != 0) return -1;

    T_DjiUserInfo user_info = {
        .appName            = USER_APP_NAME,
        .appId              = USER_APP_ID,
        .appKey             = USER_APP_KEY,
        .appLicense         = USER_APP_LICENSE,
        .developerAccountStr= USER_DEVELOPER_ACCOUNT,
        .baudRate           = USER_BAUD_RATE,
    };

    T_DjiReturnCode rc = DjiCore_Init(&user_info);
    if (rc != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        log_error(TAG, "DjiCore_Init failed (0x%08X)", rc);
        return -1;
    }

    /* Redirect PSDK internal log */
    DjiLogger_AddConsole(DJI_LOG_LEVEL_DEBUG, DJI_LOG_COLOR_OFF,
                         false, psdk_log_callback);

    rc = DjiCore_SetAlias("PSDK-Bridge");
    if (rc != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
        log_warn(TAG, "DjiCore_SetAlias failed (non-fatal)");

    rc = DjiCore_ApplicationStart();
    if (rc != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        log_error(TAG, "DjiCore_ApplicationStart failed (0x%08X)", rc);
        return -1;
    }

    log_info(TAG, "PSDK initialised and application started");
    return 0;
}

void psdk_deinit(void) {
    DjiCore_DeInit();
    log_info(TAG, "PSDK de-initialised");
}

#else /* PSDK_REAL not defined */
/* ─────────────────────────────────────────────────────────────────────────
 * Stub init
 * ───────────────────────────────────────────────────────────────────────── */

int psdk_init(void) {
    psdk_hal_register();
    log_info(TAG, "PSDK stub init OK (no real SDK linked)");
    return 0;
}

void psdk_deinit(void) {
    log_info(TAG, "PSDK stub deinit");
}

#endif /* PSDK_REAL */
