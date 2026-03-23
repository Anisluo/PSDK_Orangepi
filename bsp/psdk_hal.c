/*
 * psdk_hal.c — OrangePi Zero3 PSDK Hardware Abstraction Layer
 *
 * Implements the DJI PSDK platform interface:
 *   DjiPlatform_RegOsalHandler()    — Linux pthreads + clock_gettime
 *   DjiPlatform_RegHalUartHandler() — POSIX termios UART
 *   DjiPlatform_RegHalNetworkHandler() — Linux network socket
 *
 * Build with PSDK_REAL=1 to enable real PSDK integration.
 * Default (stub) build compiles without PSDK headers for development.
 */

#include "psdk_hal.h"
#include "../core/log/log.h"

#define TAG "bsp.hal"

#ifdef PSDK_REAL
/* ─────────────────────────────────────────────────────────────────────────
 * Real PSDK HAL — requires DJI PSDK headers in include path
 * ───────────────────────────────────────────────────────────────────────── */

#include <dji_platform.h>
#include <hal/hal_uart.h>
#include <hal/hal_network.h>
#include <osal/osal.h>
#include <osal/osal_fs.h>
#include <osal/osal_socket.h>

/* Forward declarations (implemented below) */
static T_DjiReturnCode hal_uart_init(const char *port, uint32_t baud,
                                      T_DjiUartHandle *handle);
static T_DjiReturnCode hal_uart_deinit(T_DjiUartHandle handle);
static T_DjiReturnCode hal_uart_write(T_DjiUartHandle handle,
                                       const uint8_t *buf, uint32_t len,
                                       uint32_t *written);
static T_DjiReturnCode hal_uart_read(T_DjiUartHandle handle,
                                      uint8_t *buf, uint32_t len,
                                      uint32_t *read_len);
static T_DjiReturnCode hal_uart_get_status(E_DjiHalUartNum uart_num,
                                            T_DjiUartStatus *status);

#include <termios.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>

typedef struct {
    int fd;
} UartCtx;

static speed_t baud_to_speed(uint32_t baud) {
    switch (baud) {
        case 9600:   return B9600;
        case 19200:  return B19200;
        case 38400:  return B38400;
        case 57600:  return B57600;
        case 115200: return B115200;
        case 230400: return B230400;
        case 460800: return B460800;
        case 921600: return B921600;
        default:     return B460800;
    }
}

static T_DjiReturnCode hal_uart_init(const char *port, uint32_t baud,
                                      T_DjiUartHandle *handle) {
    UartCtx *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) return DJI_ERROR_SYSTEM_MODULE_CODE_MEMORY_ALLOC_FAILED;

    ctx->fd = open(port, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (ctx->fd < 0) {
        log_error(TAG, "open(%s) failed: %s", port, strerror(errno));
        free(ctx);
        return DJI_ERROR_SYSTEM_MODULE_CODE_UNKNOWN;
    }

    struct termios tty;
    memset(&tty, 0, sizeof(tty));
    tcgetattr(ctx->fd, &tty);

    speed_t speed = baud_to_speed(baud);
    cfsetispeed(&tty, speed);
    cfsetospeed(&tty, speed);

    tty.c_cflag  =  (tty.c_cflag & ~CSIZE) | CS8;
    tty.c_cflag |=  CLOCAL | CREAD;
    tty.c_cflag &= ~(PARENB | CSTOPB | CRTSCTS);
    tty.c_lflag  =  0;
    tty.c_oflag  =  0;
    tty.c_iflag &= ~(IXON | IXOFF | IXANY | IGNBRK | BRKINT |
                     PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 0;

    if (tcsetattr(ctx->fd, TCSANOW, &tty) != 0) {
        log_error(TAG, "tcsetattr failed: %s", strerror(errno));
        close(ctx->fd);
        free(ctx);
        return DJI_ERROR_SYSTEM_MODULE_CODE_UNKNOWN;
    }
    tcflush(ctx->fd, TCIFLUSH);

    *handle = (T_DjiUartHandle)ctx;
    log_info(TAG, "UART %s @ %u baud open (fd=%d)", port, baud, ctx->fd);
    return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

static T_DjiReturnCode hal_uart_deinit(T_DjiUartHandle handle) {
    UartCtx *ctx = (UartCtx *)handle;
    if (ctx) { close(ctx->fd); free(ctx); }
    return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

static T_DjiReturnCode hal_uart_write(T_DjiUartHandle handle,
                                       const uint8_t *buf, uint32_t len,
                                       uint32_t *written) {
    UartCtx *ctx = (UartCtx *)handle;
    ssize_t n = write(ctx->fd, buf, len);
    if (n < 0) return DJI_ERROR_SYSTEM_MODULE_CODE_UNKNOWN;
    *written = (uint32_t)n;
    return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

static T_DjiReturnCode hal_uart_read(T_DjiUartHandle handle,
                                      uint8_t *buf, uint32_t len,
                                      uint32_t *read_len) {
    UartCtx *ctx = (UartCtx *)handle;
    ssize_t n = read(ctx->fd, buf, len);
    if (n < 0) {
        if (errno == EAGAIN) { *read_len = 0; return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS; }
        return DJI_ERROR_SYSTEM_MODULE_CODE_UNKNOWN;
    }
    *read_len = (uint32_t)n;
    return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

static T_DjiReturnCode hal_uart_get_status(E_DjiHalUartNum uart_num,
                                            T_DjiUartStatus *status) {
    (void)uart_num;
    status->isConnect = true;
    return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

int psdk_hal_register(void) {
    T_DjiHalUartHandler uart_handler = {
        .UartInit      = hal_uart_init,
        .UartDeInit    = hal_uart_deinit,
        .UartWriteData = hal_uart_write,
        .UartReadData  = hal_uart_read,
        .UartGetStatus = hal_uart_get_status,
    };

    T_DjiReturnCode rc = DjiPlatform_RegHalUartHandler(&uart_handler);
    if (rc != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        log_error(TAG, "DjiPlatform_RegHalUartHandler failed (0x%08X)", rc);
        return -1;
    }

    /* Register Linux OSAL (provided by PSDK samples/platform/linux) */
    extern T_DjiReturnCode DjiPlatform_RegOsalHandler(const T_DjiOsalHandler *);
    extern T_DjiOsalHandler g_osalHandler; /* from psdk linux sample osal */
    rc = DjiPlatform_RegOsalHandler(&g_osalHandler);
    if (rc != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        log_error(TAG, "DjiPlatform_RegOsalHandler failed (0x%08X)", rc);
        return -1;
    }

    log_info(TAG, "PSDK HAL registered (UART=%s)", PSDK_HAL_UART_DEV);
    return 0;
}

#else /* PSDK_REAL not defined — stub for development */
/* ─────────────────────────────────────────────────────────────────────────
 * Stub HAL — no PSDK dependency, for compilation and testing without SDK
 * ───────────────────────────────────────────────────────────────────────── */

int psdk_hal_register(void) {
    log_info(TAG, "HAL stub: PSDK not linked (build with PSDK_REAL=1)");
    return 0;
}

#endif /* PSDK_REAL */
