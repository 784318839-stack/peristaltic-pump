#!/usr/bin/env python3
"""
蠕动泵 PC 端命令行工具
通过 USB CDC 串口 (COM 口) 发送 JSON 命令控制蠕动泵

用法:
  python pump_cli.py status          # 查询状态
  python pump_cli.py start           # 启动泵
  python pump_cli.py pause           # 暂停泵
  python pump_cli.py stop            # 停止泵
  python pump_cli.py set_flow 75.0   # 设置流量 75 mL/min
  python pump_cli.py set_volume 25.0 # 设置目标体积 25 mL
  python pump_cli.py set_mode JET    # 切换为喷射模式
  python pump_cli.py monitor         # 持续监控 (Ctrl+C 退出)
  python pump_cli.py --port COM5 ... # 指定端口

依赖: pip install pyserial
"""

import sys
import json
import time
import argparse
from typing import Optional

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    print("请先安装 pyserial: pip install pyserial")
    sys.exit(1)


BAUD = 115200
TIMEOUT = 2.0


def find_pump_port() -> Optional[str]:
    """自动搜索蠕动泵的串口"""
    ports = serial.tools.list_ports.comports()
    for port in ports:
        # ESP32-S3 通常显示为 "USB Serial Device" 或 "Silicon Labs CP210x"
        if any(kw in port.description for kw in
               ["USB Serial", "CP210", "CH340", "CH343", "ESP32", "Silicon Labs"]):
            return port.device
        # 也检查 VID:PID
        if port.vid and port.pid:
            # Silicon Labs CP210x: 10C4:EA60
            # CH340: 1A86:7523
            # ESP32-S3 USB-OTG: 303A:1001
            if (port.vid, port.pid) in [(0x10C4, 0xEA60), (0x1A86, 0x7523),
                                          (0x303A, 0x1001), (0x303A, 0x4001)]:
                return port.device
    return None


def send_command(ser: serial.Serial, cmd: str, params: dict = None) -> dict:
    """发送 JSON 命令到泵, 返回响应"""
    msg = {"cmd": cmd, "params": params or {}}
    line = json.dumps(msg) + "\n"
    ser.write(line.encode())

    # 读响应 (可能多行)
    response = None
    deadline = time.time() + TIMEOUT
    while time.time() < deadline:
        raw = ser.readline()
        if not raw:
            continue
        try:
            resp = json.loads(raw.decode().strip())
            if resp.get("type") == "response":
                response = resp
            elif resp.get("type") == "telemetry":
                # 打印遥测
                print_telemetry(resp)
        except (json.JSONDecodeError, UnicodeDecodeError):
            pass

    return response or {"ok": False, "error": "No response"}


def print_telemetry(d: dict):
    """格式化打印遥测"""
    mode_icons = {"VOLUME": "📐", "TIME": "⏱", "JET": "💨"}
    icon = mode_icons.get(d.get("mode", ""), "❓")
    state = d.get("state", "?")
    flow = d.get("flow", 0)
    disp = d.get("dispensed", 0)
    target = d.get("targetVol", 0)
    progress = d.get("progress", 0)
    bar = "█" * (progress // 5) + "░" * (20 - progress // 5)

    print(f"\r{icon} {state:8s} | 流量:{flow:6.1f} | {bar} {progress:3d}% | "
          f"已出:{disp:.2f}/{target:.1f} mL", end="", flush=True)


def main():
    parser = argparse.ArgumentParser(description="蠕动泵 PC CLI 控制工具")
    parser.add_argument("--port", "-p", help="串口号 (如 COM3), 留空自动检测")
    parser.add_argument("--list", "-l", action="store_true", help="列出所有可用串口")

    sub = parser.add_subparsers(dest="action", help="命令")

    # 运行控制
    sub.add_parser("start", help="启动泵")
    sub.add_parser("pause", help="暂停泵")
    sub.add_parser("resume", help="恢复泵")
    sub.add_parser("stop", help="停止泵")
    sub.add_parser("status", help="查询状态")

    # 模式
    p_mode = sub.add_parser("set_mode", help="切换模式")
    p_mode.add_argument("mode", choices=["VOLUME", "TIME", "JET"])

    # 参数
    p_flow = sub.add_parser("set_flow", help="设置流量 mL/min")
    p_flow.add_argument("value", type=float)
    p_vol = sub.add_parser("set_volume", help="设置目标体积 mL")
    p_vol.add_argument("value", type=float)
    p_time = sub.add_parser("set_time", help="设置目标时间 秒")
    p_time.add_argument("value", type=float)

    # 喷射
    p_jv = sub.add_parser("set_jet_vol", help="设置喷射单次量 mL")
    p_jv.add_argument("value", type=float)
    p_ji = sub.add_parser("set_jet_interval", help="设置喷射间隔 秒")
    p_ji.add_argument("value", type=float)
    p_jf = sub.add_parser("set_jet_flow", help="设置喷射流量 mL/min")
    p_jf.add_argument("value", type=float)
    p_jp = sub.add_parser("set_jet_pressure", help="设置喷射压力 1-10")
    p_jp.add_argument("value", type=int)

    # 液体
    p_liq = sub.add_parser("set_liquid", help="选择液体")
    p_liq.add_argument("index", type=int, choices=[0, 1, 2, 3])

    # 其他
    sub.add_parser("prime_start", help="开始预灌")
    sub.add_parser("prime_stop", help="停止预灌")
    sub.add_parser("monitor", help="持续监控遥测数据")

    # 预设
    p_load = sub.add_parser("preset_load", help="加载方案预设")
    p_load.add_argument("slot", type=int, choices=[0, 1, 2, 3])
    p_save = sub.add_parser("preset_save", help="保存方案预设")
    p_save.add_argument("slot", type=int, choices=[0, 1, 2, 3])

    # 高级设置
    p_ad = sub.add_parser("set_anti_drip", help="设置回吸量 mL (0=关闭)")
    p_ad.add_argument("value", type=float)
    p_tl = sub.add_parser("set_tube_life", help="设置管路寿命 mL (0=不提醒)")
    p_tl.add_argument("value", type=float)

    # 校准向导
    sub.add_parser("calib_enter", help="进入校准向导")
    p_cl = sub.add_parser("calib_select_liquid", help="校准: 选择液体")
    p_cl.add_argument("index", type=int, choices=[0, 1, 2, 3])
    p_csv = sub.add_parser("calib_set_vol", help="校准: 设定校准体积")
    p_csv.add_argument("value", type=float)
    sub.add_parser("calib_start_run", help="校准: 启动校准泵")
    sub.add_parser("calib_stop_run", help="校准: 停止校准泵")
    p_cm = sub.add_parser("calib_measure", help="校准: 输入量筒实测体积")
    p_cm.add_argument("value", type=float)
    sub.add_parser("calib_save", help="校准: 确认保存新校准值")
    sub.add_parser("calib_abort", help="校准: 放弃校准")

    # WiFi
    sub.add_parser("wifi_restart", help="重启 WiFi (应用新配置)")

    args = parser.parse_args()

    # 列出串口
    if args.list:
        ports = serial.tools.list_ports.comports()
        if not ports:
            print("未发现任何串口")
        else:
            print("可用串口:")
            for p in ports:
                print(f"  {p.device} — {p.description} (VID:{p.vid:04X} PID:{p.pid:04X})")
        return

    # 找串口
    port = args.port or find_pump_port()
    if not port:
        print("未找到蠕动泵串口, 请用 --port 指定")
        print("可用串口:")
        for p in serial.tools.list_ports.comports():
            print(f"  {p.device} — {p.description}")
        sys.exit(1)

    print(f"连接到 {port}...")

    try:
        ser = serial.Serial(port, BAUD, timeout=0.1)
        time.sleep(0.5)  # 等待 ESP32 复位完成

        # 读取 hello 消息
        hello = ser.readline()
        if hello:
            try:
                h = json.loads(hello.decode().strip())
                if h.get("type") == "hello":
                    print(f"设备: {h.get('device', '?')} v{h.get('version', '?')}")
            except:
                pass

        if not args.action or args.action == "status":
            resp = send_command(ser, "get_state")
            if resp.get("ok") and "data" in resp:
                d = resp["data"]
                print(f"\n模式: {d.get('mode', '?')}  液体: {d.get('liquid', '?')}")
                print(f"状态: {d.get('state', '?')}  流量: {d.get('flow', 0):.1f} mL/min")
                print(f"目标: {d.get('targetVol', 0):.1f} mL  已出: {d.get('dispensed', 0):.2f} mL")
                print(f"累计: {d.get('totalDispensed', 0):.0f} mL  管路寿命: {d.get('tubePct', 0)}%")
                if d.get('mode') == 'JET':
                    print(f"喷射: {d.get('jetVolume', 0):.1f}mL × {d.get('jetInterval', 0):.0f}s  "
                          f"压力:{d.get('jetPressure', 0):.0f}  次数:{d.get('jetCount', 0)}")
            else:
                print(f"查询失败: {resp}")

        elif args.action == "monitor":
            print("监控模式 (Ctrl+C 退出)...")
            try:
                while True:
                    send_command(ser, "get_state")
                    time.sleep(0.5)
            except KeyboardInterrupt:
                print("\n已退出监控")

        elif args.action == "set_mode":
            resp = send_command(ser, "set_mode", {"mode": args.mode})
            print("✅ 已切换" if resp.get("ok") else f"❌ {resp.get('error')}")

        elif args.action in ("set_flow", "set_volume", "set_time",
                             "set_jet_vol", "set_jet_interval", "set_jet_flow",
                             "set_jet_pressure", "set_anti_drip", "set_tube_life"):
            val = args.value
            resp = send_command(ser, args.action, {"value": val})
            print(f"✅ 已设置 {args.action}={val}" if resp.get("ok") else f"❌ {resp.get('error')}")

        elif args.action == "set_liquid":
            resp = send_command(ser, "set_liquid", {"index": args.index})
            print("✅ 已切换液体" if resp.get("ok") else f"❌ {resp.get('error')}")

        elif args.action in ("preset_load", "preset_save"):
            resp = send_command(ser, args.action, {"slot": args.slot})
            print(f"✅ 方案{args.slot+1}" if resp.get("ok") else f"❌ {resp.get('error')}")

        elif args.action == "calib_select_liquid":
            resp = send_command(ser, "calib_select_liquid", {"index": args.index})
            print("✅ 已选液体" if resp.get("ok") else f"❌ {resp.get('error')}")

        elif args.action in ("calib_set_vol", "calib_measure"):
            resp = send_command(ser, args.action, {"value": args.value})
            print(f"✅ {args.action}={args.value}" if resp.get("ok") else f"❌ {resp.get('error')}")

        else:
            resp = send_command(ser, args.action)
            print("✅ 完成" if resp.get("ok") else f"❌ {resp.get('error')}")

        ser.close()

    except serial.SerialException as e:
        print(f"串口错误: {e}")
        sys.exit(1)


if __name__ == "__main__":
    main()
