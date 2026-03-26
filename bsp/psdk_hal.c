/*
 * psdk_hal.c — PSDK 3.9.2 platform registration for OrangePi Zero3
 *
 * Connection mode: DJI_USE_UART_AND_USB_BULK_DEVICE
 *   M3E E-Port USB2.0 = USB HOST (active, enumerates payload device)
 *   OrangePi USB-C    = USB DEVICE (gadget mode, enumerated by M3E)
 *
 *   Run setup_gadget.sh first, then connect OrangePi USB-C to M3E E-Port.
 *   M3E will enumerate OrangePi and see:
 *     /dev/ttyGS0        — CDC-ACM gadget serial (PSDK UART channel)
 *     /dev/usb-ffs/bulk1 — USB Bulk FunctionFS   (PSDK data channel)
 *
 * Registers all platform handlers required by DJI PSDK before DjiCore_Init():
 *   1. OSAL      — pthreads + clock_gettime  (from SDK osal/osal.c)
 *   2. UART      — /dev/ttyGS0 gadget serial (from SDK hal/hal_uart.c)
 *   3. USB Bulk  — /dev/usb-ffs/bulk1        (from SDK hal/hal_usb_bulk.c)
 *   4. Socket    — POSIX sockets             (from SDK osal/osal_socket.c)
 *   5. FileSystem— POSIX fs                  (from SDK osal/osal_fs.c)
 *
 * Build modes:
 *   PSDK_REAL=1  — links against libpayloadsdk.a, full hardware support
 *   (default)    — stub, compiles without SDK, for development
 */

#include "psdk_hal.h"
#include "../core/log/log.h"

#define TAG "bsp.hal"

/* ══════════════════════════════════════════════════════════════════════════
 * REAL PSDK implementation
 * ══════════════════════════════════════════════════════════════════════════ */
#ifdef PSDK_REAL

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dji_platform.h>
#include <dji_logger.h>

/* SDK sample HAL/OSAL — compiled alongside this file via Makefile */
#include "dji_sdk_config.h"
#include "osal.h"
#include "osal_fs.h"
#include "osal_socket.h"

#if (CONFIG_HARDWARE_CONNECTION != DJI_USE_ONLY_USB_BULK_DEVICE) && \
    (CONFIG_HARDWARE_CONNECTION != DJI_USE_ONLY_NETWORK_DEVICE)
#include "hal_uart.h"
#endif

#if (CONFIG_HARDWARE_CONNECTION == DJI_USE_UART_AND_NETWORK_DEVICE)
#include "hal_network.h"
#endif

#if (CONFIG_HARDWARE_CONNECTION == DJI_USE_UART_AND_USB_BULK_DEVICE) || \
    (CONFIG_HARDWARE_CONNECTION == DJI_USE_ONLY_USB_BULK_DEVICE)
#include "hal_usb_bulk.h"
#endif

#if (CONFIG_HARDWARE_CONNECTION == DJI_USE_UART_AND_USB_BULK_DEVICE) || \
    (CONFIG_HARDWARE_CONNECTION == DJI_USE_ONLY_USB_BULK_DEVICE)
typedef struct {
    const char *path;
    int fd;
} T_PsdkPreparedFd;

typedef struct {
    const char *pattern;
    const char *label;
    uint32_t seen;
} T_PsdkLogFilter;

static T_PsdkPreparedFd g_prepared_bulk_fds[] = {
    { PSDK_HAL_USB_BULK_EP_OUT,  -1 },
    { PSDK_HAL_USB_BULK_EP_IN,   -1 },
    { PSDK_HAL_USB_BULK2_EP_OUT, -1 },
    { PSDK_HAL_USB_BULK2_EP_IN,  -1 },
    { PSDK_HAL_USB_BULK3_EP_OUT, -1 },
    { PSDK_HAL_USB_BULK3_EP_IN,  -1 },
    { PSDK_HAL_USB_BULK6_EP_OUT, -1 },
    { PSDK_HAL_USB_BULK6_EP_IN,  -1 },
};

static T_PsdkLogFilter g_psdk_log_filters[] = {
    { "cmdset: 0x22 cmdid: 0xAD", "unsupported cmd 0x22/0xAD", 0 },
    { "cmdset: 0x21 cmdid: 0x05", "unsupported cmd 0x21/0x05", 0 },
    { "cmdset: 0x0F cmdid: 0x74", "unsupported cmd 0x0F/0x74", 0 },
    { "cmdset: 0x49 cmdid: 0x00", "unsupported cmd 0x49/0x00", 0 },
};
#endif

/* ── Console callback: forward PSDK internal logs to our logger ─────────── */
static T_DjiReturnCode psdk_console_cb(const uint8_t *data, uint16_t data_len) {
    /* data is NOT NUL-terminated; copy and strip trailing newline */
    char buf[512];
    size_t copy = data_len < sizeof(buf) - 1 ? data_len : sizeof(buf) - 1;
    memcpy(buf, data, copy);
    buf[copy] = '\0';
    /* strip trailing \r\n */
    while (copy > 0 && (buf[copy - 1] == '\n' || buf[copy - 1] == '\r'))
        buf[--copy] = '\0';

#if (CONFIG_HARDWARE_CONNECTION == DJI_USE_UART_AND_USB_BULK_DEVICE) || \
    (CONFIG_HARDWARE_CONNECTION == DJI_USE_ONLY_USB_BULK_DEVICE)
    if (copy > 0) {
        size_t i;

        for (i = 0; i < sizeof(g_psdk_log_filters) / sizeof(g_psdk_log_filters[0]); ++i) {
            T_PsdkLogFilter *filter = &g_psdk_log_filters[i];

            if (strstr(buf, filter->pattern) == NULL)
                continue;

            filter->seen++;
            if (filter->seen <= 3) {
                log_debug("bsp.psdk", "%s", buf);
            } else if (filter->seen == 4 || (filter->seen % 50) == 0) {
                log_debug("bsp.psdk", "%s recurring (%u hits suppressed)",
                          filter->label, filter->seen - 3);
            }
            return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
        }
    }
#endif

    if (copy > 0)
        log_debug("bsp.psdk", "%s", buf);
    return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

int psdk_hal_register(void) {
    T_DjiReturnCode rc;
    const char *internal_log_level = getenv("PSDK_INTERNAL_LOG_LEVEL");
    E_DjiLoggerConsoleLogLevel console_level = DJI_LOGGER_CONSOLE_LOG_LEVEL_INFO;

    if (internal_log_level != NULL &&
        (strcmp(internal_log_level, "debug") == 0 || strcmp(internal_log_level, "DEBUG") == 0)) {
        console_level = DJI_LOGGER_CONSOLE_LOG_LEVEL_DEBUG;
    }

    /* ── 1. OSAL ─────────────────────────────────────────────────────────── */
    T_DjiOsalHandler osal_handler = {
        .TaskCreate        = Osal_TaskCreate,
        .TaskDestroy       = Osal_TaskDestroy,
        .TaskSleepMs       = Osal_TaskSleepMs,
        .MutexCreate       = Osal_MutexCreate,
        .MutexDestroy      = Osal_MutexDestroy,
        .MutexLock         = Osal_MutexLock,
        .MutexUnlock       = Osal_MutexUnlock,
        .SemaphoreCreate   = Osal_SemaphoreCreate,
        .SemaphoreDestroy  = Osal_SemaphoreDestroy,
        .SemaphoreWait     = Osal_SemaphoreWait,
        .SemaphoreTimedWait= Osal_SemaphoreTimedWait,
        .SemaphorePost     = Osal_SemaphorePost,
        .Malloc            = Osal_Malloc,
        .Free              = Osal_Free,
        .GetTimeMs         = Osal_GetTimeMs,
        .GetTimeUs         = Osal_GetTimeUs,
        .GetRandomNum      = Osal_GetRandomNum,
    };
    rc = DjiPlatform_RegOsalHandler(&osal_handler);
    if (rc != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        log_error(TAG, "DjiPlatform_RegOsalHandler failed (0x%08X)", rc);
        return -1;
    }

    /* ── 2. UART ─────────────────────────────────────────────────────────── */
#if (CONFIG_HARDWARE_CONNECTION != DJI_USE_ONLY_USB_BULK_DEVICE) && \
    (CONFIG_HARDWARE_CONNECTION != DJI_USE_ONLY_NETWORK_DEVICE)
    T_DjiHalUartHandler uart_handler = {
        .UartInit      = HalUart_Init,
        .UartDeInit    = HalUart_DeInit,
        .UartWriteData = HalUart_WriteData,
        .UartReadData  = HalUart_ReadData,
        .UartGetStatus = HalUart_GetStatus,
    };
    rc = DjiPlatform_RegHalUartHandler(&uart_handler);
    if (rc != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        log_error(TAG, "DjiPlatform_RegHalUartHandler failed (0x%08X)", rc);
        return -1;
    }
#endif

    /* ── 3. Logger (must be after OSAL + UART) ───────────────────────────── */
    T_DjiLoggerConsole console = {
        .func           = psdk_console_cb,
        .consoleLevel   = console_level,
        .isSupportColor = false,
    };
    rc = DjiLogger_AddConsole(&console);
    if (rc != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
        log_warn(TAG, "DjiLogger_AddConsole failed (non-fatal, 0x%08X)", rc);

    /* ── 4. Transport-specific handler ───────────────────────────────────── */
#if (CONFIG_HARDWARE_CONNECTION == DJI_USE_UART_AND_USB_BULK_DEVICE) || \
    (CONFIG_HARDWARE_CONNECTION == DJI_USE_ONLY_USB_BULK_DEVICE)
    T_DjiHalUsbBulkHandler usb_bulk_handler = {
        .UsbBulkInit = HalUsbBulk_Init,
        .UsbBulkDeInit = HalUsbBulk_DeInit,
        .UsbBulkWriteData = HalUsbBulk_WriteData,
        .UsbBulkReadData = HalUsbBulk_ReadData,
        .UsbBulkGetDeviceInfo = HalUsbBulk_GetDeviceInfo,
    };
    rc = DjiPlatform_RegHalUsbBulkHandler(&usb_bulk_handler);
    if (rc != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        log_error(TAG, "DjiPlatform_RegHalUsbBulkHandler failed (0x%08X)", rc);
        return -1;
    }
    log_info(TAG, "Registered transport: %s",
#if (CONFIG_HARDWARE_CONNECTION == DJI_USE_ONLY_USB_BULK_DEVICE)
             "USB Bulk only");
#else
             "UART + USB Bulk");
#endif
#elif (CONFIG_HARDWARE_CONNECTION == DJI_USE_UART_AND_NETWORK_DEVICE)
    T_DjiHalNetworkHandler network_handler = {
        .NetworkInit = HalNetWork_Init,
        .NetworkDeInit = HalNetWork_DeInit,
        .NetworkGetDeviceInfo = HalNetWork_GetDeviceInfo,
    };
    rc = DjiPlatform_RegHalNetworkHandler(&network_handler);
    if (rc != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        log_error(TAG, "DjiPlatform_RegHalNetworkHandler failed (0x%08X)", rc);
        return -1;
    }
    log_info(TAG, "Registered transport: UART + Network");
#else
    log_info(TAG, "Registered transport: UART only");
#endif

    /* ── 5. Socket handler ───────────────────────────────────────────────── */
    T_DjiSocketHandler socket_handler = {
        .Socket      = Osal_Socket,
        .Bind        = Osal_Bind,
        .Close       = Osal_Close,
        .UdpSendData = Osal_UdpSendData,
        .UdpRecvData = Osal_UdpRecvData,
        .TcpListen   = Osal_TcpListen,
        .TcpAccept   = Osal_TcpAccept,
        .TcpConnect  = Osal_TcpConnect,
        .TcpSendData = Osal_TcpSendData,
        .TcpRecvData = Osal_TcpRecvData,
    };
    rc = DjiPlatform_RegSocketHandler(&socket_handler);
    if (rc != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        log_error(TAG, "DjiPlatform_RegSocketHandler failed (0x%08X)", rc);
        return -1;
    }

    /* ── 6. File system handler ──────────────────────────────────────────── */
    T_DjiFileSystemHandler fs_handler = {
        .FileOpen  = Osal_FileOpen,
        .FileClose = Osal_FileClose,
        .FileWrite = Osal_FileWrite,
        .FileRead  = Osal_FileRead,
        .FileSync  = Osal_FileSync,
        .FileSeek  = Osal_FileSeek,
        .DirOpen   = Osal_DirOpen,
        .DirClose  = Osal_DirClose,
        .DirRead   = Osal_DirRead,
        .Mkdir     = Osal_Mkdir,
        .Unlink    = Osal_Unlink,
        .Rename    = Osal_Rename,
        .Stat      = Osal_Stat,
    };
    rc = DjiPlatform_RegFileSystemHandler(&fs_handler);
    if (rc != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        log_error(TAG, "DjiPlatform_RegFileSystemHandler failed (0x%08X)", rc);
        return -1;
    }

    log_info(TAG, "PSDK HAL registered (UART=%s)", PSDK_HAL_UART_DEV);
    return 0;
}

int psdk_hal_usb_bulk_prepare(void) {
#if (CONFIG_HARDWARE_CONNECTION == DJI_USE_UART_AND_USB_BULK_DEVICE)
    size_t i;
    bool prepared_any = false;

    for (i = 0; i < sizeof(g_prepared_bulk_fds) / sizeof(g_prepared_bulk_fds[0]); ++i) {
        if (g_prepared_bulk_fds[i].fd >= 0) {
            prepared_any = true;
            continue;
        }

        g_prepared_bulk_fds[i].fd = open(g_prepared_bulk_fds[i].path, O_RDWR);
        if (g_prepared_bulk_fds[i].fd < 0) {
            if (errno == ENOENT || errno == ENODEV) {
                continue;
            }

            log_error(TAG, "open bulk endpoint failed (%s): %s",
                      g_prepared_bulk_fds[i].path, strerror(errno));
            psdk_hal_usb_bulk_release();
            return -1;
        }
        prepared_any = true;
    }

    if (!prepared_any) {
        log_error(TAG, "no USB bulk endpoints available before DjiCore_Init");
        return -1;
    }

    log_info(TAG, "Prepared USB Bulk endpoints before DjiCore_Init");
#elif (CONFIG_HARDWARE_CONNECTION == DJI_USE_ONLY_USB_BULK_DEVICE)
    /* Manifold3 mode is USB-bulk-only. Let the SDK own endpoint open/close to
     * avoid conflicting with its multi-channel initialization flow. */
#endif
    return 0;
}

void psdk_hal_usb_bulk_release(void) {
#if (CONFIG_HARDWARE_CONNECTION == DJI_USE_UART_AND_USB_BULK_DEVICE) || \
    (CONFIG_HARDWARE_CONNECTION == DJI_USE_ONLY_USB_BULK_DEVICE)
    size_t i;
    for (i = 0; i < sizeof(g_prepared_bulk_fds) / sizeof(g_prepared_bulk_fds[0]); ++i) {
        if (g_prepared_bulk_fds[i].fd >= 0) {
            close(g_prepared_bulk_fds[i].fd);
            g_prepared_bulk_fds[i].fd = -1;
        }
    }
#endif
}

#else /* ── STUB ─────────────────────────────────────────────────────────── */

int psdk_hal_register(void) {
    log_info(TAG, "HAL stub: build with PSDK_REAL=1 for real hardware");
    return 0;
}

int psdk_hal_usb_bulk_prepare(void) {
    return 0;
}

void psdk_hal_usb_bulk_release(void) {
}

#endif /* PSDK_REAL */
