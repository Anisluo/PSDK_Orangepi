# PSDK 当前调试结论

**硬件**: OrangePi Zero3 (aarch64) + CH340 USB-UART + M3E E-Port Development Kit  
**目标**: 让 `psdkd` 稳定完成 `DjiCore_Init`，抓取无人机状态信息，并验证 KMZ 航线上传

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

3. **启动具有明显偶发性，但已经找到更有效的启动时序**
   - 远端多次自动拉起曾回到：
   - `DjiCore_Init failed (0x000000E1)`
   - `Payload negotiate error, returnCode = 225`
   - 但在调整 [tools/start_dual.sh](/home/aniston/Desktop/PSDK/tools/start_dual.sh) 后，已经确认：
   - **不要为了等 `carrier` 阻塞第一次 `psdkd` 启动**
   - `usb0` 一出现就尽快预配地址并启动 `psdkd`，成功率明显更高
   - 当前更准确的表述是：
   - **系统并非“永远协商失败”**
   - 而是**存在明显启动时序敏感性**

---

## 关键现象整理

### 已确认工作正常的部分

1. **UART 链路正常**
   - OrangePi 使用 CH340，对应 `/dev/ttyUSB0`
   - 已可稳定识别机型和波特率

2. **USB Gadget / RNDIS 枚举正常**
   - 使用 VID/PID `0x0955:0x7020`
   - 网卡名为 `usb0`
   - [tools/start_dual.sh](/home/aniston/Desktop/PSDK/tools/start_dual.sh) 会重建 gadget，并在启动前预配：
   - `192.168.5.3/24`

3. **SDK negotiate 逻辑可成功完成**
   - 成功样例里不仅协商完成，还进入了后续初始化流程
   - 说明问题不是“SDK 逻辑绝对不支持当前硬件”

4. **启动成功依赖外部时序**
   - 从现象看，和以下因素有关：
   - E-Port USB 物理连接时机
   - `usb0` 枚举完成时机
   - `psdkd` 启动时 negotiate 时间窗
   - 目前验证过更有效的策略是：
   - `usb0` 出现后立即启动 `psdkd`
   - `carrier` 只作为诊断参考，不要阻塞第一次启动

### 当前仍不稳定的部分

1. **`psdkd` 冷启动/自动拉起仍有偶发失败**
   - 失败时会停在 negotiate 阶段
   - 返回 `0x000000E1`
   - UDP 服务不会启动

2. **成功与失败都受启动窗口影响**
   - 一旦错过窗口，会表现为：
   - `Waiting payload negotiate finish.`
   - 最终 `0xE1`

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

## 状态信息抓取结论

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

### 当前已实测抓到的状态信息

在 M3E 开机、未飞行状态下，已通过 OrangePi 查询到：

1. `drone.get_telemetry`
   - `alt_rel_m = -2.66`
   - `heading_deg = 66.9`
   - `battery_pct = 4`
   - `battery_mv = 14588`
   - `motors_on = false`
   - `flight_status = 0`

2. `drone.get_battery_info`
   - `remaining_pct = 4`
   - `voltage_mv = 14588`
   - `current_ma = -4202`
   - `temperature_dc = 432`，约 `43.2°C`

3. 本机轮询脚本也已验证可用
   - [tools/watch_drone_status_local.sh](/home/aniston/Desktop/PSDK/tools/watch_drone_status_local.sh)
   - [tools/open_drone_status_terminal.sh](/home/aniston/Desktop/PSDK/tools/open_drone_status_terminal.sh)

### 当前仍未稳定返回的字段

以下字段目前仍经常是 0：

1. `lat` / `lon`
2. `gps_sats`
3. `gps_fix`
4. `rtk`

这说明：

- 电量、高度、航向、飞行状态这类基础状态已经能读到
- 但 GPS / RTK 相关数据还需要继续排查是否为机体当前状态、订阅频率或 topic 映射问题

---

## KMZ 上传测试结论

已经在现有 `psdkd` 中新增了一个最小 RPC 上传接口：

1. 方法名：`drone.upload_kmz`
2. 文件目录：`/home/orangepi/PSDK/kmz_data`
3. 当前只做**上传**，不自动执行航线

相关文件：

1. [bsp/drone_ctrl.c](/home/aniston/Desktop/PSDK/bsp/drone_ctrl.c)
2. [bsp/drone_ctrl.h](/home/aniston/Desktop/PSDK/bsp/drone_ctrl.h)
3. [core/handler/handler.c](/home/aniston/Desktop/PSDK/core/handler/handler.c)

### 实测结果

成功上传：

1. `10kV酒厂001.kmz`
2. `10kV酒厂002.kmz`

失败上传：

1. `10kV酒厂002甲.kmz`

### 失败原因

失败不是链路问题，而是文件校验失败。日志里明确出现：

1. `DjiWaypointV3_UploadKmzFile failed (0x000000FF)`
2. `Check kmz file md5sum failed, error: 0x000000F2`

这说明：

- OrangePi 到 M3E 的 KMZ 上传链路已经打通
- 失败文件已经传到飞机侧参与校验
- 被拒绝的是文件内容或格式，而不是传输通道

---

## 当前调试结论

### 主结论

**现在的核心问题不是“SDK 永远不会走到 socket”，而是 `psdkd` 启动时序敏感；一旦成功启动，状态读取和 KMZ 上传都已经可以工作。**

当启动成功时：

1. negotiate 能完成
2. `HalNetWork_Init` 会被调用
3. `DjiCore_Init` 能越过鉴权并进入后续模块
4. PSDK 订阅数据可用
5. 可以通过 UDP JSON-RPC 读到无人机状态
6. 可以把部分 KMZ 文件上传到 M3E

当启动失败时：

1. 日志停在 negotiate 阶段
2. 返回 `0x000000E1`
3. UDP 服务不会启动
4. 所有状态读取和 KMZ 上传都会超时

---

## 下一步最值得做的事

1. **继续提升启动稳定性**
   - 重点围绕 [tools/start_dual.sh](/home/aniston/Desktop/PSDK/tools/start_dual.sh)
   - 保持“`usb0` 一出现就尽快启动 `psdkd`”这个原则
   - 继续观察 E-Port 供电冷启动场景下的成功率

2. **继续补齐 GPS / RTK 字段**
   - 重点排查：
   - 当前机体状态是否满足 GPS/RTK 输出条件
   - 订阅 topic 是否还需要补充
   - 当前字段映射是否准确

3. **继续验证 KMZ 文件兼容性**
   - 对比成功文件和失败文件内容差异
   - 重点看 `002甲.kmz` 为什么会触发校验失败

4. **保留一份成功启动与成功上传的完整日志**
   - 重点保留：
   - `Payload negotiate has finished`
   - `HalNetWork_Init`
   - `drone_ctrl initialised`
   - `ready — listening on UDP port 5555`
   - `Request upload kmz file success`
   - `Check kmz file md5sum success`

---

## 补充说明

本地还修正了一个误导日志的问题：

- [bsp/psdk_hal.h](/home/aniston/Desktop/PSDK/bsp/psdk_hal.h)

之前日志里 UART 设备名硬编码成 `/dev/ttyS5`，会误导判断；现已改为优先使用编译参数中的 `LINUX_UART_DEV1`，和实际使用的 `/dev/ttyUSB0` 对齐。
