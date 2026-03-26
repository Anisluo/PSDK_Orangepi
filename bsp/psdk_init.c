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
#include <dji_core.h>
#include "dji_sdk_app_info.h"   /* fill in your credentials here */

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

    T_DjiReturnCode rc = DjiCore_Init(&user_info);
    if (rc != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        log_error(TAG, "DjiCore_Init failed (0x%08X)", rc);
        psdk_hal_usb_bulk_release();
        return -1;
    }
    log_info(TAG, "DjiCore_Init OK");

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
