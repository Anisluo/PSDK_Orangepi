/*
 * dji_sdk_config.h — OrangePi Zero3 hardware connection configuration
 *
 * Hardware: OrangePi Zero3 + DJI E-Port → Mavic 3T
 *
 * Default connection mode: DJI_USE_ONLY_UART
 *   E-Port uses UART (/dev/ttyS5 @ 460800 baud) for auth/control.
 *
 * You can switch to USB Bulk or UART+Network by changing
 * CONFIG_HARDWARE_CONNECTION below.
 */

#ifndef DJI_SDK_CONFIG_H
#define DJI_SDK_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/* Connection mode selection */
#define DJI_USE_ONLY_UART                (0)
#define DJI_USE_UART_AND_USB_BULK_DEVICE (1)
#define DJI_USE_UART_AND_NETWORK_DEVICE  (2)

/* UART+USB Bulk: matches libpayloadsdk.a build mode */
#define CONFIG_HARDWARE_CONNECTION  DJI_USE_UART_AND_NETWORK_DEVICE

/*
 * E-Port USB network adapter interface name on OrangePi Zero3 (Ubuntu Noble).
 * Override at compile time:  -DLINUX_NETWORK_DEV=\"usb1\"
 */
#ifndef LINUX_NETWORK_DEV
#define LINUX_NETWORK_DEV  "usb0"
#endif

#ifdef __cplusplus
}
#endif

#endif /* DJI_SDK_CONFIG_H */
