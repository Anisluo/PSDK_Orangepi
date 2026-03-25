在 OrangePi 上执行：


sudo tee /etc/systemd/system/psdkd.service << 'EOF'
[Unit]
Description=DJI PSDK Bridge Daemon
After=network.target

[Service]
Type=simple
User=root
ExecStart=/home/orangepi/PSDK/build/bin/psdkd --ip 0.0.0.0 --port 5555
Restart=on-failure
RestartSec=5
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target
EOF

# 启用并启动
sudo systemctl daemon-reload
sudo systemctl enable psdkd
sudo systemctl start psdkd

# 查看状态
sudo systemctl status psdkd
常用管理命令：


sudo systemctl start psdkd      # 启动
sudo systemctl stop psdkd       # 停止
sudo systemctl restart psdkd    # 重启
sudo journalctl -u psdkd -f     # 实时查看日志