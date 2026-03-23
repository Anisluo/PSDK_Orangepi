/*
 * dji_sdk_app_info.h — DJI Payload SDK application credentials
 *
 * IMPORTANT: Fill in your own credentials from:
 *   https://developer.dji.com/user/apps/
 *
 * Do NOT commit this file to a public repository once filled in.
 *
 * Steps to obtain credentials:
 *   1. Register at https://developer.dji.com
 *   2. Create a new application (select "Payload SDK")
 *   3. Copy App Name, App ID, App Key, App License
 *   4. Use your DJI developer account email as DEVELOPER_ACCOUNT
 */

#ifndef DJI_SDK_APP_INFO_H
#define DJI_SDK_APP_INFO_H

#ifdef __cplusplus
extern "C" {
#endif

/* ── Fill in your credentials below ─────────────────────────────────────── */
#define USER_APP_NAME           "your_app_name"
#define USER_APP_ID             "your_app_id"
#define USER_APP_KEY            "your_app_key"
#define USER_APP_LICENSE        "your_app_license"
#define USER_DEVELOPER_ACCOUNT  "your_developer_account@email.com"

/* Baud rate for E-Port UART (DJI default: 460800) */
#define USER_BAUD_RATE          "460800"

#ifdef __cplusplus
}
#endif

#endif /* DJI_SDK_APP_INFO_H */
