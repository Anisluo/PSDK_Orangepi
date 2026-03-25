# PSDK 当前调试结论

**硬件**: OrangePi Zero3 (aarch64) + CH340 USB-UART + M3E E-Port Development Kit  
**目标**: 让 `psdkd` 稳定完成 `DjiCore_Init`，并抓取无人机状态信息

---

## 最新结论

之前文档里“根本问题一定在 negotiate 流程本身、`HalNetWork_Init` 从未被调用”的结论，已经被实机结果推翻。

### 目前可以确认的事实

1. **negotiate 不是必然失败**
   - 远程实跑时，曾明确出现：
   - `Payload negotiate has finished.`
   - `HalNetWork_Init: ip=192.168.90.2 mask=255.255.0.0`
   - 这说明：
   - `cmd=5/cmd=7` 路径能走通
   - SDK 确实会调用 `HalNetWork_Init`
   - 网络地址是**动态分配**的，不固定是 `192.168.5.x`

2. **`0x000000E3` 鉴权失败曾经出现过，但不是当前主问题**
   - 一次运行中出现过：
   - `DjiAuth_Sha256RsaVerify: Application information verification failed`
   - `DjiCore_Init failed (0x000000E3)`
   - 之后对远端做了 `make clean` + 全量重编，曾成功越过该阶段，进入后续模块初始化和订阅流程。
   - 这说明当时更像是**旧二进制 / 凭据不一致**导致的偶发问题，不是当前最主要阻塞。

3. **当前主问题是启动具有明显偶发性**
   - 远端最近多次自动拉起，都回到了：
   - `DjiCore_Init failed (0x000000E1)`
   - `Payload negotiate error, returnCode = 225`
   - 当前更准确的表述是：
   - **系统并非“永远协商失败”**
   - 而是**存在时序/枚举/连接窗口问题，导致有时 negotiate 成功，有时超时**

---

## 关键现象整理

### 已确认工作正常的部分

1. **UART 链路正常**
   - OrangePi 使用 CH340，对应 `/dev/ttyUSB0`
   - 已可稳定识别机型和波特率

2. **USB Gadget / RNDIS 枚举正常**
   - 使用 VID/PID `0x0955:0x7020`
   - 网卡名为 `usb0`
   - `tools/start_dual.sh` 会重建 gadget，并在启动前预配：
   - `192.168.5.3/24`

3. **SDK negotiate 逻辑曾成功完成**
   - 成功样例里不仅协商完成，还进入了后续初始化流程
   - 说明问题不是“SDK 逻辑绝对不支持当前硬件”

### 当前仍不稳定的部分

1. **`psdkd` 冷启动/自动拉起不稳定**
   - 最近多次远端后台启动，`/tmp/psdk_run.log` 停在：
   - `Waiting payload negotiate finish.`
   - 最终 `0xE1`

2. **日志文件会停留在旧失败记录**
   - 最近检查到 `/tmp/psdk_run.log` 时间戳停在 `2026-03-25 08:47:25 UTC`
   - 所以后续自动重试看到的相同报错，部分是**旧日志复用**，不能简单等同于“每次都完整执行到同一位置”

3. **启动成功依赖外部时序**
   - 从现象看，和以下因素有关：
   - E-Port USB 物理连接时机
   - `usb0` 枚举完成时机
   - `psdkd` 启动时 negotiate 时间窗

---

## 对旧结论的修正

### 已被推翻的判断

以下结论不再成立：

1. `HalNetWork_Init` 从未被调用
2. SDK 一定在 `HalNetWork_Init` 之前通过 `ioctl(SIOCGIFADDR, "usb0")` 读取 Linux 本地 IP
3. `0xE1` 一定是 negotiate 逻辑本身的永久性缺陷

### 目前更可信的判断

1. **negotiate 是可成功的，但存在竞态**
2. **动态 IP 才是实际运行路径**
   - 成功样例的 payload IP 是 `192.168.90.2/16`
   - 之前看到的 `192.168.107.1` 更像 UAV 侧动态地址
   - `192.168.5.3 / 192.168.5.10` 更像兼容默认值或预配热启动地址
3. **当前最值得处理的是“稳定拉起”而不是继续猜 `cmd=5` 内部实现**

---

## SDK 内部实现的补充结论

对 `libpayloadsdk.a` 反汇编后，已确认：

1. 命令映射大致为：
   - `cmd=1` -> `DjiPayloadNegotiate_VersionHandle`
   - `cmd=3` -> `DjiPayloadNegotiate_DeviceInfoHandle`
   - `cmd=5` -> `DjiPayloadNegotiate_NotifyHandle`
   - `cmd=7` -> `DjiPayloadNegotiate_SetIpAddrHandle`

2. `cmd=5` 的 notify 成功处理依赖：
   - notify 包可解析
   - cap info 合法
   - device name 为 `opsdk_net`

3. `DjiPayloadNegotiate_GetLocalAddr()` 更像读取 SDK 内部协商保存的地址，而不是直接去读 Linux 网卡配置。

这组信息仍然有价值，但它解释的是**状态机如何前进**，不能再直接推出“当前主问题一定在 `cmd=5`”。

---

## 状态信息抓取能力

代码里已经具备无人机状态读取接口，只要 `psdkd` 成功启动并监听 UDP 端口 `5555`，就可以通过 JSON-RPC 获取状态。

### 启动成功后的关键标志

1. `drone_ctrl initialised (PSDK subscriptions active)`
2. `ready — listening on UDP port 5555`

### 当前已实现的状态接口

位于：
- [app/main.c](/home/aniston/Desktop/PSDK/app/main.c)
- [core/handler/handler.c](/home/aniston/Desktop/PSDK/core/handler/handler.c)
- [bsp/drone_ctrl.c](/home/aniston/Desktop/PSDK/bsp/drone_ctrl.c)

支持的方法：

1. `system.ping`
2. `drone.get_telemetry`
3. `drone.get_battery_info`
4. `drone.get_gimbal_angle`
5. `drone.get_camera_state`
6. `drone.get_rtk_status`

### `drone.get_telemetry` 当前能返回的字段

1. `lat` / `lon`
2. `alt_msl_m` / `alt_rel_m`
3. `vx_ms` / `vy_ms` / `vz_ms`
4. `heading_deg`
5. `battery_pct` / `battery_mv`
6. `gps_sats` / `gps_fix`
7. `flight_status`
8. `motors_on`

### 数据来源

当前代码已订阅这些 PSDK topic：

1. `POSITION_FUSED` @ 10Hz
2. `VELOCITY` @ 10Hz
3. `QUATERNION` @ 10Hz
4. `GPS_DETAILS` @ 1Hz
5. `STATUS_FLIGHT` @ 1Hz
6. `BATTERY_INFO` @ 1Hz
7. `GIMBAL_ANGLES` @ 10Hz

这说明：

- **理论上完全可以抓到无人机状态**
- 当前没抓到实时返回值，不是接口没写，而是最近几次 `psdkd` 没稳定启动成功，UDP `5555` 没起来

---

## 当前调试结论

### 主结论

**现在的核心问题不是“SDK 永远不会走到 socket”，而是 `psdkd` 启动存在明显偶发性。**

当启动成功时：

1. negotiate 能完成
2. `HalNetWork_Init` 会被调用
3. `DjiCore_Init` 能越过鉴权并进入后续模块
4. PSDK 订阅数据可用
5. 理论上可以通过 UDP JSON-RPC 读到无人机状态

当启动失败时：

1. 日志停在 negotiate 阶段
2. 返回 `0x000000E1`
3. UDP 服务不会启动
4. 所有状态读取都会超时

---

## 下一步最值得做的事

1. **先把启动稳定性打下来**
   - 给 `tools/start_dual.sh` 增加更明确的：
   - 日志重定向
   - 启动前清理
   - `usb0` 出现后的状态确认
   - 成功/失败判定和自动重试

2. **一旦成功启动，立刻抓状态**
   - 依次调用：
   - `system.ping`
   - `drone.get_telemetry`
   - `drone.get_battery_info`
   - `drone.get_gimbal_angle`
   - `drone.get_camera_state`
   - `drone.get_rtk_status`

3. **保留一份成功启动的完整日志**
   - 重点保留：
   - `Payload negotiate has finished`
   - `HalNetWork_Init`
   - `drone_ctrl initialised`
   - `ready — listening on UDP port 5555`

---

## 补充说明

本地还修正了一个误导日志的问题：

- [bsp/psdk_hal.h](/home/aniston/Desktop/PSDK/bsp/psdk_hal.h)

之前日志里 UART 设备名硬编码成 `/dev/ttyS5`，会误导判断；现已改为优先使用编译参数中的 `LINUX_UART_DEV1`，和实际使用的 `/dev/ttyUSB0` 对齐。
