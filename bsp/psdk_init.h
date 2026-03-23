#ifndef PSDK_INIT_H
#define PSDK_INIT_H

/*
 * psdk_init.h — PSDK lifecycle management
 *
 * Call psdk_init() once at startup (after psdk_hal_register()).
 * Call psdk_deinit() at shutdown.
 *
 * App identity (product name, version, serial number) must be filled
 * in dji_sdk_app_info.h when real PSDK SDK is integrated.
 */

int  psdk_init(void);
void psdk_deinit(void);

#endif /* PSDK_INIT_H */
