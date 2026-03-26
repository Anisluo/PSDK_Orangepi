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
#define USER_APP_NAME           "PSDKOrangepi"
#define USER_APP_ID             "181392"
#define USER_APP_KEY            "fe71d62687213a4401e6afe8cb746bd"
#define USER_APP_LICENSE        "P0rYPnomRTA6aVK8Qp53G3saZlEmnlkGjvYzML2ieUmAARvvnXeiBtut8xHt40snA5TxoHL+TNB7ZHNPSlfFayCRDcvaMf5rJ5ce7SmFRGWrY+QGRPTUmnxoARcDfUhnmrmEQezQU91BWA94hWVj6GCrN2kMAZjRNiYJPgE8+Pj0A5qzvGIUp5ssXbNmNERl/p7TLwd4S7Ln8MkhcLhTScjrVgz5EsYFz0RU6WrCy95wNK33nEaDhfHDBZ/Iu55tTObqPOe3FTb+xDQ1DyYEkYVwIQnlbLHXUGdqeji1AndKZ4wltcEaXBIA9JiJ/Hj4Ij3bloKzpS9C3Z3PMLKYJA=="
#define USER_DEVELOPER_ACCOUNT  "2010057290@qq.com"

/* Baud rate for E-Port UART. M3E/M3T/M3TD all use 921600. */
#define USER_BAUD_RATE          "921600"

#ifdef __cplusplus
}
#endif

#endif /* DJI_SDK_APP_INFO_H */
