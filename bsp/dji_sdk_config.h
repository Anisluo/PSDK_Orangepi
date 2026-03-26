/*
 * dji_sdk_config.h — OrangePi Zero3 hardware connection configuration
 *
 * Hardware: OrangePi Zero3 + DJI E-Port → Mavic 3T
 *
 * Default connection mode: DJI_USE_ONLY_UART
 *   E-Port uses UART (/dev/ttyS5 @ 921600 baud) for auth/control on M3TD.
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
#define DJI_USE_ONLY_USB_BULK_DEVICE     (3)
#define DJI_USE_ONLY_NETWORK_DEVICE      (4)

/* Default: M3TD (Matrice 3D Series) — UART + USB Bulk gadget
 * Override at compile time via Makefile DRONE_MODEL=M3E/M3T/M3TD */
#ifndef CONFIG_HARDWARE_CONNECTION
#define CONFIG_HARDWARE_CONNECTION  DJI_USE_UART_AND_USB_BULK_DEVICE
#endif

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
