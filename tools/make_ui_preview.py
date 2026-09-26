#!/usr/bin/env python3
"""Assemble the standalone Web UI preview from peristaltic_pump/index.html.

    python tools/make_ui_preview.py            # 生成到桌面（可用 --out 改）
    python tools/make_ui_preview.py --check    # 只检查预览是否已过期，不写文件

产出的 HTML = 固件 UI（CSS / 结构 / 前端 JS，逐字取自 index.html）
             + 预览专用外壳（tools/ui_preview_template.html）
             + 模拟后端（tools/ui_preview_sim.js）

它**不参与固件构建**：设备上跑的页面是 index.html 经 generate_web_ui.py 压进
web_ui_gen.h 的那份。改完 index.html 或模拟后端后重跑本脚本，预览才会跟上；
--check 用于确认预览没有落后于 index.html（退出码 1 = 已过期）。

模拟后端的语义要和 command_protocol.cpp / pump_machine.cpp / pump_core.cpp 保持一致，
tools/test_ui_preview_sim.js 里有一组断言盯着这件事。
"""
import argparse
import hashlib
import re
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parent
INDEX = ROOT / "peristaltic_pump" / "index.html"
TEMPLATE = TOOLS / "ui_preview_template.html"
SIM = TOOLS / "ui_preview_sim.js"
DEFAULT_OUT = Path.home() / "Desktop" / "蠕动泵UI预览-DM542.html"

# 预览专用的命令日志面板，插在 <main> 开头（固件 UI 里没有这一块）
SIMLOG = (
    '<details id="simLog" open>\n'
    "  <summary>命令日志（模拟后端的收发，含固件的校验报错）</summary>\n"
    '  <div id="simLogBody"></div>\n'
    "</details>"
)

# index.html 没有、但窄屏下需要的 flex 修正（只加在预览里，不动固件 UI）
CSS_TWEAK_OLD = (
    ".input-row input{flex:1;background:var(--bg);border:1px solid var(--border);"
    "border-radius:8px;padding:10px 12px;color:var(--text);font-size:16px;outline:none}"
)
CSS_TWEAK_NEW = CSS_TWEAK_OLD[:-1] + ";min-width:0}"


def build() -> str:
    ix = INDEX.read_text(encoding="utf-8").replace("\r\n", "\n")

    css = re.search(r"<style>\n(.*?)</style>", ix, re.S)
    if not css:
        sys.exit("index.html: <style> block not found")
    ix_css = css.group(1).rstrip("\n")

    start = ix.index("<header>")
    end = ix.index("<script>", start)
    ix_markup = ix[start:end].rstrip("\n")

    scripts = re.findall(r"<script>\n(.*?)</script>", ix, re.S)
    if len(scripts) != 1:
        sys.exit(f"index.html: expected exactly 1 <script> block, got {len(scripts)}")
    ix_ui = scripts[0].rstrip("\n")

    if "min-width:0" not in ix_css:
        if CSS_TWEAK_OLD not in ix_css:
            sys.exit("index.html: .input-row input rule changed - update CSS_TWEAK_OLD")
        ix_css = ix_css.replace(CSS_TWEAK_OLD, CSS_TWEAK_NEW)

    if "<main>\n" not in ix_markup:
        sys.exit("index.html: <main> not found, cannot inject the sim log panel")
    ix_markup = ix_markup.replace(
        "<main>\n", "<main>\n  " + SIMLOG.replace("\n", "\n  ") + "\n\n", 1)

    tpl = TEMPLATE.read_text(encoding="utf-8").replace("\r\n", "\n")
    sim = SIM.read_text(encoding="utf-8").replace("\r\n", "\n").rstrip("\n")

    out = (tpl.replace("{{FIRMWARE_CSS}}", ix_css)
              .replace("{{MARKUP}}", ix_markup)
              .replace("{{SIM_BACKEND}}", sim)
              .replace("{{UI_SCRIPT}}", ix_ui))

    for ph in ("{{FIRMWARE_CSS}}", "{{MARKUP}}", "{{SIM_BACKEND}}", "{{UI_SCRIPT}}"):
        if ph in out:
            sys.exit(f"template placeholder {ph} was not substituted")
    if out.count("<script>") != 2 or out.count("</script>") != 2:
        sys.exit("assembled preview does not have exactly 2 <script> blocks")
    if '<main>\n  <details id="simLog"' not in out:
        sys.exit("sim log panel was not injected into <main>")
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", type=Path, default=DEFAULT_OUT,
                    help=f"输出路径（默认 {DEFAULT_OUT}）")
    ap.add_argument("--check", action="store_true",
                    help="只比对现有文件是否已过期，不写入；过期返回 1")
    args = ap.parse_args()

    text = build()
    data = text.encode("utf-8")
    digest = hashlib.md5(data).hexdigest()

    if args.check:
        if not args.out.exists():
            print(f"STALE  {args.out} 不存在")
            return 1
        cur = args.out.read_bytes()
        if cur == data:
            print(f"OK     {args.out} 与 index.html 同步 ({len(data)} bytes, md5 {digest})")
            return 0
        print(f"STALE  {args.out} 与 index.html 不一致 "
              f"(现有 {len(cur)} bytes, 应为 {len(data)} bytes) — 重跑 make_ui_preview.py")
        return 1

    args.out.write_bytes(data)
    print(f"written {args.out}")
    print(f"        {len(data)} bytes, md5 {digest}")
    print(f"sources {INDEX.name} + {TEMPLATE.name} + {SIM.name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
