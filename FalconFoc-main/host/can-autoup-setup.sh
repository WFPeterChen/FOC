#!/usr/bin/env bash
# ============================================================
# CAN 开机/插入自动拉起 (gs_usb / candleLight 等 socketcan 设备)
# 跑一次即可持久生效, 之后插上适配器/开机 can0(can1) 自动 up @1Mbps。
#
#   sudo ./can-autoup-setup.sh          # 默认 1Mbps
#   sudo ./can-autoup-setup.sh 500000   # 自定义波特率
#
# 原理: 用 systemd-networkd 的 [Match] Type=can 匹配所有 CAN 接口并设波特率。
#       只接管 CAN 类型接口, 不影响 NetworkManager 管的其它网卡。
# 卸载: sudo rm /etc/systemd/network/80-can.network && sudo systemctl restart systemd-networkd
# ============================================================
set -euo pipefail
BITRATE="${1:-1000000}"

if [[ $EUID -ne 0 ]]; then echo "请用 sudo 运行"; exit 1; fi

cat > /etc/systemd/network/80-can.network <<EOF
[Match]
Type=can

[CAN]
BitRate=${BITRATE}
EOF
chmod 644 /etc/systemd/network/80-can.network

systemctl enable --now systemd-networkd
systemctl restart systemd-networkd

echo "✓ 已安装 /etc/systemd/network/80-can.network (BitRate=${BITRATE})"
echo "✓ systemd-networkd 已启用。重插适配器或重启后 can0/can1 会自动拉起。"
echo
echo "现在立即拉起当前已插入的接口:"
for IF in can0 can1; do
  if [[ -e /sys/class/net/$IF ]]; then
    ip link set "$IF" down 2>/dev/null || true
    if ip link set "$IF" up type can bitrate "${BITRATE}" 2>/dev/null; then
      echo "  ✓ $IF up @${BITRATE}"
    else
      echo "  · $IF 跳过(可能已由 networkd 接管)"
    fi
  fi
done
ip -br link show type can || true
