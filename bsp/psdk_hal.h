#ifndef PSDK_HAL_H
#define PSDK_HAL_H

/*
 * psdk_hal.h — OrangePi Zero3 hardware abstraction layer for DJI PSDK
 *
 * DJI PSDK requires platform-specific implementations of:
 *   - OSAL  : thread, mutex, semaphore, time
 *   - UART  : E-port physical link (typically /dev/ttyUSB0 or /dev/ttyACM0)
 *   - Network: for PSDK high-bandwidth data (video/telemetry over Ethernet)
 *
 * When PSDK SDK is available, uncomment the PSDK includes and link
 * against libpayloadsdk.a (or the shared variant).
 *
 * E-port wiring on OrangePi Zero3:
 *   - UART TX/RX via USB-UART bridge or direct 3.3V UART
 *   - Recommended: /dev/ttyUSB0 @ 460800 baud (DJI default)
 *   - Or USB bulk via /dev/usb-DJI (if using SKYPORT v2 USB mode)
 */

/* Keep log output aligned with the actual UART device passed from the build. */
#ifdef LINUX_UART_DEV1
#define PSDK_HAL_UART_DEV   LINUX_UART_DEV1
#else
#define PSDK_HAL_UART_DEV   "/dev/ttyS5"
#endif
#define PSDK_HAL_UART_BAUD  460800

/* PSDK HAL USB Bulk — FunctionFS gadget endpoints */
#define PSDK_HAL_USB_BULK_EP_OUT "/dev/usb-ffs/bulk1/ep1"
#define PSDK_HAL_USB_BULK_EP_IN  "/dev/usb-ffs/bulk1/ep2"

/* Register all PSDK platform handlers.  Call before DjiCore_Init(). */
int psdk_hal_register(void);

#endif /* PSDK_HAL_H */
