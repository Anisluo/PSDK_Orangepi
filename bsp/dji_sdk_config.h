/*
 * dji_sdk_config.h — OrangePi Zero3 hardware connection configuration
 *
 * Hardware: OrangePi Zero3 + DJI E-Port → Mavic 3T
 *
 * Connection mode: DJI_USE_UART_AND_NETWORK_DEVICE
 *   E-Port provides:
 *     • UART  (/dev/ttyUSB0 @ 460800 baud) — low-speed command channel
 *     • USB   — creates a RNDIS/CDC-ECM virtual NIC (default: usb0)
 *               used by PSDK for high-speed telemetry/video data
 *
 * To change the network interface name, define EPORT_NETDEV when building:
 *   make PSDK_REAL=1 EPORT_NETDEV=usb1
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
