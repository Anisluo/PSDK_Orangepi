# M3E 兼容 M3TD 适配记录

## 背景

OrangePi Zero3 作为 USB Gadget 设备通过 E-Port 连接 DJI 无人机，原先支持 M3E（RNDIS 模式），
现扩展支持 M3TD（USB Bulk FunctionFS 模式）。

连接模式对比：

| 型号 | SDK 连接模式 | USB 功能 | 网络/数据通道 | UART 通道 |
|------|-------------|---------|--------------|----------|
| M3E / M3T | `DJI_USE_UART_AND_NETWORK_DEVICE` | RNDIS + CDC-ACM | `usb0` (RNDIS) | `/dev/ttyGS0` (ACM) |
| M3TD | `DJI_USE_UART_AND_USB_BULK_DEVICE` | FunctionFS Bulk + CDC-ACM | `/dev/usb-ffs/bulk1` (ep1/ep2) | `/dev/ttyGS0` (ACM) |

---

## 已解决的问题

### 1. `dji_sdk_config.h` 宏覆盖 Makefile `-D` 参数

**症状：** `make DRONE_MODEL=M3TD` 编译后 psdkd 仍以 `DJI_USE_UART_AND_NETWORK_DEVICE` 运行。

**根因：** 原 `bsp/dji_sdk_config.h` 硬编码 `#define CONFIG_HARDWARE_CONNECTION DJI_USE_UART_AND_NETWORK_DEVICE`，
无 `#ifndef` 保护，导致 Makefile 传入的 `-DCONFIG_HARDWARE_CONNECTION=...` 被头文件覆盖。

**修复：**
```c
// bsp/dji_sdk_config.h
#ifndef CONFIG_HARDWARE_CONNECTION
#define CONFIG_HARDWARE_CONNECTION  DJI_USE_UART_AND_USB_BULK_DEVICE
#endif
```

---

### 2. `HalNetWork_*` 链接错误

**症状：** 编译 M3TD 时出现 `undefined reference to 'HalNetWork_Init'` 等链接错误。

**根因：** OrangePi 上残留旧版 `dji_sdk_config.h`，导致 `PSDK_ENABLE_NETWORK=1`，
Makefile 错误地把 `hal_network.c` 加进编译列表。

**修复：** rsync 同步后正确覆盖头文件；Makefile 中 M3TD 路径明确设置
`PSDK_ENABLE_NETWORK=0` / `PSDK_ENABLE_USB_BULK=1`。

---

### 3. UDC 绑定失败（`Device or resource busy`）

**症状：** `echo musb-hdrc.5.auto > /sys/kernel/config/usb_gadget/psdk/UDC` 返回
`write error: Device or resource busy`。

**根因：** FunctionFS 挂载后，内核要求 ep0 先写入 USB 描述符（进入 ACTIVE 状态）才允许绑定 UDC。
原脚本在 `rebuild_gadget()` 里直接绑 UDC，没有先运行 `ffs_init`。

**修复：** 新增 `setup_m3td_udc()` 函数，流程改为：
1. `rebuild_gadget()` 挂载 FunctionFS，**不绑 UDC**
2. 后台启动 `ffs_init`，向 ep0 写描述符
3. sleep 0.5s 后绑定 UDC
4. 等待 ep1/ep2 出现（无人机枚举完成）

---

### 4. `ffs_init` 写 strings 返回 `EINVAL`

**症状：** `ffs_init` 写完 USB 描述符后，写 strings 块时返回 `Invalid argument`，进程退出。

**根因：** sun50iw9 6.1 内核 bug：写 strings 块时返回 EINVAL，但描述符已成功写入，
FunctionFS 已进入 ACTIVE 状态。

**修复：** `tools/ffs_init.c` 中将 strings write 失败改为非致命（打印 perror 后继续）：
```c
n = write(fd, &strings, sizeof(strings));
if (n < 0) {
    perror("write strings (non-fatal, continuing)");
    /* 不退出 — 描述符已写入，内核可能已接受 */
}
```

---

### 5. 交叉编译失败

**症状：** 开发机上 `make PLATFORM=orangepi` 链接失败，找不到 aarch64 版本的
`libjson-c`、`libssl`、`libcrypto`。

**根因：** 开发机未安装 aarch64 版本的运行时库，交叉链接无法完成。

**修复：** 改为 `tools/build_deploy.sh`：rsync 源码到 OrangePi → OrangePi 本机用 gcc 编译
→ 写入 `drone_model` 配置文件 → `systemctl restart psdkd`。

---

### 6. psdkd 忽略 SIGTERM，cleanup 无效

**症状：** 重启 psdkd 时旧进程残留，多个 psdkd 实例并发运行，USB 状态混乱。

**根因：** psdkd 阻塞在 SDK 内部（`DjiCore_Init` / `DjiFcSubscription_Init` 等），
SIGTERM 信号被屏蔽或忽略，进程无法终止。

**修复：** `cleanup_processes()` 改为 SIGTERM + 1s 延迟 + SIGKILL：
```bash
pkill -x psdkd 2>/dev/null || true
pkill -f ffs_init 2>/dev/null || true
sleep 1
pkill -9 -x psdkd 2>/dev/null || true
pkill -9 -f ffs_init 2>/dev/null || true
sleep 0.5
```

---

### 7. start_and_watch 超时后 `wait` 永久阻塞，脚本不再重试

**症状：** psdkd 超过 `NEGOTIATE_TIMEOUT` 后，`start_and_watch` 打印超时日志，
但之后脚本永久卡住，不再进入下一轮重试或 cleanup。

**根因：** 超时后 `kill "$pid"` 发的是 SIGTERM，psdkd 不响应（见问题 6）；
紧接着的 `wait "$pid"` 因进程未死而**永久阻塞**，导致主循环永远走不到
下一个 `cleanup_processes()` / SIGKILL。

**修复：** 在超时路径的 `wait` 之前加 SIGKILL：
```bash
kill "$pid" 2>/dev/null || true
sleep 1
kill -9 "$pid" 2>/dev/null || true
wait "$pid" 2>/dev/null || true
return 1
```

---

## 已解决的问题（续）

### 8. M3TD UART 设备路径错误（/dev/ttyGS0 → /dev/ttyUSB0）

**物理连接确认：**
- E-Port UART 引脚 → **USB 转 UART 适配器** → OrangePi USB 口 → `/dev/ttyUSB0`
- E-Port USB2.0 Type-C（Host）→ **双头 Type-C 线** → OrangePi OTG（Gadget 模式）→ USB Bulk

**症状：** `DjiCore_Init` 长时间循环尝试所有波特率（115200/230400/460800/921600/1000000），
246s 后报 `Try identify UART0 connection failed`。

**根因：** M3TD gadget 的 UART 设备配置为 `/dev/ttyGS0`（USB CDC-ACM 虚拟串口），
但实际 UART 通过物理 USB-to-UART 适配器进来，应为 `/dev/ttyUSB0`。
CDC-ACM 路径上无人机从不发送数据，故 UART 握手永远无法完成。

**修复：** Makefile 中 M3TD 分支改为：
```makefile
DRONE_UART_DEV ?= /dev/ttyUSB0
```
同时去掉 M3TD gadget 中多余的 `acm.GS0` 函数（UART 不走 USB CDC-ACM）。

**效果：** 修复后 SDK 在 ~1.7s 内识别到 `Matrice 3D Series` + `Extension Port Type`，
UART 波特率协商成功为 921600 bps。

---

## 当前未解决的问题

### Payload negotiate 超时（returnCode = 225 = TIMEOUT）

**现象：** UART 物理通信正常后，`DjiCore_Init` 在 ~8.8s 时失败：

```
[adapter]-[Info] Identify aircraft series is Matrice 3D Series
[adapter]-[Info] Identify mount position type is Extension Port Type
[adapter]-[Info] Identity uart0 baudrate is 921600 bps
[adapter]-[Info] Waiting payload negotiate finish.   ← 每 1s 打印，共 5 次
[adapter]-[Error] Payload negotiate error, returnCode = 225
```

`returnCode = 225 = 0xE1 = DJI_ERROR_SYSTEM_MODULE_RAW_CODE_TIMEOUT`，是超时，
不是凭证错误。

**时序分析：**

| 时刻 | 事件 |
|------|------|
| t=0.825s | 无人机在 UART 上响应 |
| t=1.714s | SDK 识别机型：Matrice 3D Series，Extension Port |
| t=1.776s | UART 波特率协商完成：921600 bps |
| t=1.776s | SDK 发送 negotiate 请求（`cmd 0x0F01/0x0087`）→ 无人机回应 4 字节 |
| t=3.8s | SDK 重试 negotiate → 无人机再次回应 4 字节 |
| t=3.8s–8.8s | "Waiting payload negotiate finish."（5 次，每秒一次） |
| t=8.8s | Timeout，returnCode=225 |

**根本原因（推断）：** `DjiCore_Init` 内部的 negotiate 完成信号可能由无人机通过
**USB Bulk 通道**发回（写向 OrangePi 的 ep2 端点）。但在 `DjiCore_Init` 未成功之前，
`HalUsbBulk_Init`（打开 ep1/ep2）还未被 SDK 调用，FunctionFS 的 ep2 没有进程打开，
内核会 NAK 掉无人机的写入，完成信号丢失，最终 5 秒后超时。

```
DjiCore_Init 需要 negotiate 完成
  → negotiate 完成信号需要 ep2 已打开（可接收）
  → ep2 通过 HalUsbBulk_Init 打开
  → HalUsbBulk_Init 在 DjiCore_Init 成功后才被 SDK 调用
```

**补充观察：**
- 无人机持续发送 `cmdset 0x49 cmdid 0x00`（来自 0x1204→0x0A06），
  SDK 报 "received unsupport request cmd that noneed ack"，可能是 negotiate 完成通知，
  但 beta 版 SDK（V3.9.2-beta.0-build.2125）不处理该命令集。

**待验证的解决方向：**

1. **提前激活 ep2：** 在 `start_dual.sh` 中，ffs_init 写完描述符后（ep1/ep2 文件出现，
   但 UDC 尚未绑定时），先通过代理进程打开 ep2 并保持活跃，
   再绑定 UDC 启动 psdkd，使无人机的 negotiate 完成信号能被接收。

2. **延长启动等待：** 在 ep1/ep2 出现后延迟 20–30s 再启动 psdkd，
   让无人机的 PSDK 层有足够初始化时间。

3. **升级 SDK 版本：** 当前使用 V3.9.2-beta，可能存在 M3TD negotiate
   命令集处理 bug；尝试使用 stable 版本。

---

## 相关文件

| 文件 | 说明 |
|------|------|
| `tools/start_dual.sh` | 启动脚本，支持 M3E（RNDIS）和 M3TD（FunctionFS）两种模式 |
| `tools/ffs_init.c` | FunctionFS ep0 描述符初始化程序（M3TD 专用） |
| `tools/build_deploy.sh` | 构建部署脚本，接受 `M3E`/`M3T`/`M3TD` 参数 |
| `bsp/dji_sdk_config.h` | SDK 连接模式宏定义（有 `#ifndef` 保护） |
| `bsp/psdk_hal.c` | PSDK HAL 注册（OSAL、UART、USB Bulk、Socket、FS） |
| `Makefile` | 按 `DRONE_MODEL` 选择编译选项和传输模式 |
| `platform/orangepi/platform.mk` | OrangePi aarch64 平台配置 |
