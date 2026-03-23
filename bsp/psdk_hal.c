/*
 * psdk_hal.c — PSDK 3.9.2 platform registration for OrangePi Zero3
 *
 * Registers all platform handlers required by DJI PSDK before DjiCore_Init():
 *   1. OSAL   — pthreads + clock_gettime  (from SDK osal/osal.c)
 *   2. UART   — termios /dev/ttyUSB0      (from SDK hal/hal_uart.c)
 *   3. Network— USB RNDIS adapter (usb0)  (from SDK hal/hal_network.c)
 *   4. Socket — POSIX sockets             (from SDK osal/osal_socket.c)
 *   5. FileSystem — POSIX fs              (from SDK osal/osal_fs.c)
 *
 * Hardware connection: DJI_USE_UART_AND_NETWORK_DEVICE
 *   E-Port USB creates a RNDIS network adapter on Linux.
 *   The default interface name is "usb0"; override via Makefile:
 *     make PSDK_REAL=1 EPORT_NETDEV=usb1
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

#include <stdio.h>
#include <dji_platform.h>
#include <dji_logger.h>

/* SDK sample HAL/OSAL — compiled alongside this file via Makefile */
#include "hal_uart.h"
#include "hal_network.h"
#include "osal.h"
#include "osal_fs.h"
#include "osal_socket.h"

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
    if (copy > 0)
        log_debug("bsp.psdk", "%s", buf);
    return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

int psdk_hal_register(void) {
    T_DjiReturnCode rc;

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

    /* ── 3. Logger (must be after OSAL + UART) ───────────────────────────── */
    T_DjiLoggerConsole console = {
        .func           = psdk_console_cb,
        .consoleLevel   = DJI_LOGGER_CONSOLE_LOG_LEVEL_DEBUG,
        .isSupportColor = false,
    };
    rc = DjiLogger_AddConsole(&console);
    if (rc != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
        log_warn(TAG, "DjiLogger_AddConsole failed (non-fatal, 0x%08X)", rc);

    /* ── 4. Network HAL (E-Port RNDIS USB adapter) ───────────────────────── */
    T_DjiHalNetworkHandler net_handler = {
        .NetworkInit          = HalNetWork_Init,
        .NetworkDeInit        = HalNetWork_DeInit,
        .NetworkGetDeviceInfo = HalNetWork_GetDeviceInfo,
    };
    rc = DjiPlatform_RegHalNetworkHandler(&net_handler);
    if (rc != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        log_error(TAG, "DjiPlatform_RegHalNetworkHandler failed (0x%08X)", rc);
        return -1;
    }

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

    log_info(TAG, "PSDK HAL registered (UART=%s NET=%s)",
             PSDK_HAL_UART_DEV, PSDK_HAL_NET_DEV);
    return 0;
}

#else /* ── STUB ─────────────────────────────────────────────────────────── */

int psdk_hal_register(void) {
    log_info(TAG, "HAL stub: build with PSDK_REAL=1 for real hardware");
    return 0;
}

#endif /* PSDK_REAL */
