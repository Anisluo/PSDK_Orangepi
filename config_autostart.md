# OrangePi 开机自启动配置

目标：让 OrangePi 在被 M3E E-Port 供电后自动启动 `tools/start_dual.sh`，并由脚本负责：

1. 清理旧进程和旧 gadget 状态
2. 重建 RNDIS gadget
3. 等待 `usb0` 真正有 `carrier`
4. 启动 `psdkd`
5. 失败后自动重试

---

## 方案说明

不要直接把 `psdkd` 二进制做成自启动。

正确做法是把下面这个脚本做成自启动入口：

- [tools/start_dual.sh](/home/aniston/Desktop/PSDK/tools/start_dual.sh)

原因：

1. `psdkd` 依赖 USB gadget / `usb0` 状态
2. 真实问题经常出在 gadget 和链路时序，而不是 `psdkd` 本身
3. `start_dual.sh` 已经包含了重建 gadget、等待链路、失败重试的逻辑

仓库里已经提供了对应的 `systemd` 服务文件：

- [tools/psdkd.service](/home/aniston/Desktop/PSDK/tools/psdkd.service)

---

## 配置步骤

推荐直接执行一键脚本：

```bash
cd /home/orangepi/PSDK
sudo bash tools/setup_orangepi_autostart.sh
```

如果你想只安装并启用，但暂时不立即启动：

```bash
cd /home/orangepi/PSDK
sudo bash tools/setup_orangepi_autostart.sh --no-start
```

等价的手工步骤如下：

```bash
cd /home/orangepi/PSDK

sudo install -m 644 tools/psdkd.service /etc/systemd/system/psdkd.service
sudo chmod +x /home/orangepi/PSDK/tools/start_dual.sh

sudo systemctl daemon-reload
sudo systemctl enable psdkd.service
sudo systemctl restart psdkd.service
```

---

## 验证方法

查看服务状态：

```bash
sudo systemctl status psdkd.service --no-pager -l
```

实时查看日志：

```bash
sudo journalctl -u psdkd.service -f
```

查看脚本自己的启动日志：

```bash
sudo tail -n 200 /tmp/psdk_boot.log
sudo tail -n 200 /tmp/psdk_attempt.log
```

查看 UDP 服务是否起来：

```bash
ss -lunp | grep 5555
```

查看 `usb0` 链路状态：

```bash
ip addr show usb0
cat /sys/class/net/usb0/carrier
```

如果 `carrier=1`，说明不是只有本地 gadget 创建成功，而是链路确实接通了。

---

## 开机后预期行为

正常情况下，OrangePi 上电后会自动：

1. 启动 `psdkd.service`
2. 调用 `tools/start_dual.sh`
3. 重建 gadget
4. 等待 `usb0`
5. 等待 `usb0 carrier=1`
6. 启动 `psdkd`

如果某次启动失败，`systemd` 会重新拉起服务，脚本内部也会继续做有限次重试。

---

## 常用管理命令

启动：

```bash
sudo systemctl start psdkd.service
```

停止：

```bash
sudo systemctl stop psdkd.service
```

重启：

```bash
sudo systemctl restart psdkd.service
```

禁用开机自启：

```bash
sudo systemctl disable psdkd.service
```

---

## 当前注意事项

1. 当前自启动入口已经改为脚本，不要再直接配置：
   - `/home/orangepi/PSDK/build/bin/psdkd`

2. 如果日志里反复出现：
   - `usb0 exists but carrier is not ready yet`

说明 OrangePi 本地 gadget 已创建，但 M3E 侧没有真正把链路拉起来。

3. 如果日志里出现：
   - `DjiCore_Init failed (0x000000E1)`

说明链路已经进入更后面的协商阶段，但协商超时。

---

## 推荐检查顺序

1. 先看 `systemctl status psdkd.service`
2. 再看 `/tmp/psdk_boot.log`
3. 再看 `/tmp/psdk_attempt.log`
4. 最后看 `usb0` 的 `carrier`
