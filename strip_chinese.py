#!/usr/bin/env python3
"""Remove all Chinese font support from peristaltic_pump.ino, replace with English."""

SRC = r"C:\Users\xg821\peristaltic_pump\peristaltic_pump.ino"

with open(SRC, "r", encoding="utf-8") as f:
    content = f.read()

# ===== 1. Remove Chinese font include =====
content = content.replace('#include "pump_chinese_font.h"  // 中文字体 (124字, 3.4KB)\n', "")

# ===== 2. Remove Chinese font functions block =====
# Find and remove the entire block from "// 中文字体绘制" to just before "// 在 u8g2 当前光标位置"
# We need to find the start and end markers

# Remove the font comment block and all static functions
import re

# Remove from "// 中文字体绘制" through to the end of drawCN function
# and the drawCN_cur function
lines = content.split("\n")
new_lines = []
skip = False
for line in lines:
    if "// 用法: drawCN(x, y, " in line or "// RAM 副本" in line:
        skip = True
        continue
    if skip:
        # Stop skipping after the drawCN_cur function ends
        if line.startswith("void drawCN_cur"):
            continue  # skip this line
        if skip and ("// -----" in line or "// =" in line or "void drawMain" in line):
            skip = False
            new_lines.append(line)
            continue
        continue
    new_lines.append(line)

content = "\n".join(new_lines)

# ===== 3. Replace drawCN_cur calls with u8g2.print =====
replacements = [
    # --- drawMain header ---
    ('drawCN_cur("== 定时")', 'u8g2.print("== TIMED")'),
    ('drawCN_cur("== 喷射")', 'u8g2.print("== JET")'),
    ('drawCN_cur("== 体积")', 'u8g2.print("== VOL")'),
    # --- liquid names (variable-based, just remove drawCN_cur wrapper) ---
    # These use u8g2.print() directly
    # --- state strings ---
    ('s = tubeWarn ? "待机!" : "待机"', 's = tubeWarn ? "Idle!" : "Idle"'),
    ('s = "运行中"', 's = "Running"'),
    ('s = "已暂停"', 's = "Paused"'),
    ('s = "回吸中"', 's = "Anti-drip"'),
    ('s = tubeWarn ? "完成!" : "已完成"', 's = tubeWarn ? "Done!" : "Done"'),
    ('s = jetSquirting ? "喷射中" : "等待中"', 's = jetSquirting ? "Squirting" : "Waiting"'),
    # --- drawMain content ---
    ('drawCN_cur("流量(自动):")', 'u8g2.print("Flow(auto):")'),
    ('drawCN_cur("设:")', 'u8g2.print("Set:")'),
    ('drawCN_cur("已出:")', 'u8g2.print("Out:")'),
    ('drawCN_cur("过:")', 'u8g2.print("Elap:")'),
    ('drawCN_cur("单次:")', 'u8g2.print("Shot:")'),
    ('drawCN_cur("流量:")', 'u8g2.print("Flow:")'),
    ('drawCN_cur(" 压力:")', 'u8g2.print(" Pres:")'),
    ('drawCN_cur("已喷:")', 'u8g2.print("Shots:")'),
    ('drawCN_cur("下次:")', 'u8g2.print("Next:")'),
    ('drawCN_cur("目标:")', 'u8g2.print("Tgt:")'),
    # --- bottom bar ---
    ('drawCN_cur("A:体积 B:时间 ")', 'u8g2.print("A:VOL B:TIME")'),
    ('drawCN_cur("A:单次量 B:间隔 ")', 'u8g2.print("A:Shot B:Intv")'),
    ('drawCN_cur("A:流量 B:体积 ")', 'u8g2.print("A:Flow B:Vol")'),
    ('drawCN_cur("C:启动")', 'u8g2.print("C:Start")'),
    ('drawCN_cur("C:暂停")', 'u8g2.print("C:Pause")'),
    ('drawCN_cur("C:恢复")', 'u8g2.print("C:Resume")'),
    ('drawCN_cur("C:复位")', 'u8g2.print("C:Reset")'),
    ('drawCN_cur(" D:切换")', 'u8g2.print(" D:Mode")'),
    ('drawCN_cur("累计:")', 'u8g2.print("Total:")'),
    # --- set flow ---
    ('drawCN_cur("设置流量 mL/min")', 'u8g2.print("Set Flow mL/min")'),
    ('drawCN_cur("0-9:输入 #:小数点 *:确认 D:返回")', 'u8g2.print("0-9:In #:Dot *:OK D:Back")'),
    ('drawCN_cur("设置体积 mL")', 'u8g2.print("Set Volume mL")'),
    ('drawCN_cur("单次喷射量 mL")', 'u8g2.print("Shot Volume mL")'),
    ('drawCN_cur("喷射间隔 (秒)")', 'u8g2.print("Interval (sec)")'),
    ('drawCN_cur("0-9:输入 *:确认 D:返回")', 'u8g2.print("0-9:In *:OK D:Back")'),
    ('drawCN_cur("喷射压力 (1-10档)")', 'u8g2.print("Pressure (1-10)")'),
    ('drawCN_cur("1=柔和 5=标准 10=猛冲")', 'u8g2.print("1=Soft 5=Std 10=Max")'),
    ('drawCN_cur("喷射流量 mL/min")', 'u8g2.print("Jet Flow mL/min")'),
    ('drawCN_cur("2-4mm嘴:水>200 粘稠20-100")', 'u8g2.print("Tip2-4mm:W>200 T20-100")'),
    # --- jet options ---
    ('drawCN_cur("喷射模式设置")', 'u8g2.print("Jet Settings")'),
    ('drawCN_cur("A: 喷射校准")', 'u8g2.print("A: Jet Calib")'),
    ('drawCN_cur("B: 设定喷射压力 (")', 'u8g2.print("B: Jet Pres (")'),
    ('drawCN_cur("#: 设定喷射流量 (")', 'u8g2.print("#: Jet Flow (")'),
    ('drawCN_cur("A:校准 B:压力 #:流量 D:返回")', 'u8g2.print("A:Cal B:Pres #:Flow D:Back")'),
    # --- presets ---
    ('drawCN_cur("方案 ")', 'u8g2.print("Slot ")'),
    ('drawCN_cur("尚无数据, 按 # 保存当前")', 'u8g2.print("Empty, press # to save")'),
    ('drawCN_cur("模式: ")', 'u8g2.print("Mode: ")'),
    ('drawCN_cur("液体: ")', 'u8g2.print("Liq: ")'),
    ('drawCN_cur("单次:")', 'u8g2.print("Shot:")'),
    ('drawCN_cur(" 压力:")', 'u8g2.print(" Pres:")'),
    ('drawCN_cur("*:加载 #:存此处 1-4:存到对应槽 D:返回")', 'u8g2.print("*:Load #:Save 1-4:Slot D:Back")'),
    # --- select liquid ---
    ('drawCN_cur("选择液体")', 'u8g2.print("Select Liquid")'),
    ('drawCN_cur("A/B/C/D:选择 *:跳过")', 'u8g2.print("A/B/C/D:Pick *:Skip")'),
    # --- set time ---
    ('drawCN_cur("设置时间 (秒)")', 'u8g2.print("Set Time (sec)")'),
    # --- calibrate ---
    ('drawCN_cur("校准 1/5 - 选液体")', 'u8g2.print("Cal 1/5 - Pick Liq")'),
    ('drawCN_cur("A/B/C/D:选择 *:保持 #:退出")', 'u8g2.print("A/B/C/D:Pick *:Keep #:Exit")'),
    ('drawCN_cur("校准 2/5")', 'u8g2.print("Cal 2/5")'),
    ('drawCN_cur("泵出多少 mL 用于校准?")', 'u8g2.print("Calibrate with ? mL")'),
    ('drawCN_cur("0-9输入 #. *确认 D返回 #高级")', 'u8g2.print("0-9In #. *OK D:Back #:Adv")'),
    ('drawCN_cur("校准 3/5")', 'u8g2.print("Cal 3/5")'),
    ('drawCN_cur("放好量筒按 C 启动")', 'u8g2.print("Ready, press C")'),
    ('drawCN_cur("C:启动  D:返回")', 'u8g2.print("C:Start D:Back")'),
    ('drawCN_cur("完成! *:下一步")', 'u8g2.print("Done! *:Next")'),
    ('drawCN_cur("C:停止 (运行中)")', 'u8g2.print("C:Stop (running)")'),
    ('drawCN_cur("校准 4/5")', 'u8g2.print("Cal 4/5")'),
    ('drawCN_cur("量筒实测值 (mL):")', 'u8g2.print("Measured (mL):")'),
    ('drawCN_cur("0-9输入 #. *确认 D返回")', 'u8g2.print("0-9In #. *OK D:Back")'),
    ('drawCN_cur("校准 5/5 - 结果")', 'u8g2.print("Cal 5/5 - Result")'),
    ('drawCN_cur("旧值:")', 'u8g2.print("Old:")'),
    ('drawCN_cur("新值:")', 'u8g2.print("New:")'),
    ('drawCN_cur("步数:")', 'u8g2.print("Steps:")'),
    ('drawCN_cur("*:保存 D:放弃")', 'u8g2.print("*:Save D:Discard")'),
    # --- advanced settings ---
    ('drawCN_cur("高级设置")', 'u8g2.print("Advanced")'),
    ('drawCN_cur("回吸量:")', 'u8g2.print("Anti-drip:")'),
    ('drawCN_cur(" mL (0=关)")', 'u8g2.print(" mL (0=off)")'),
    ('drawCN_cur("管:")', 'u8g2.print("Tube:")'),
    ('drawCN_cur("A:水 B:粘稠 C:有机 D:自定义")', 'u8g2.print("A:W B:T C:O D:C")'),
    ('drawCN_cur("0-9 #. *确认 D取消")', 'u8g2.print("0-9 #. *OK D:Cancel")'),
    ('drawCN_cur("A:回吸 B:管寿 C:选液 *:返回")', 'u8g2.print("A:Drip B:Tube C:Liq *:Back")'),
    # --- prime ---
    ('drawCN_cur("流量:2000 mL/min")', 'u8g2.print("Flow:2000 mL/min")'),
    ('drawCN_cur("C:启停  D:退出")', 'u8g2.print("C:Run/Stop D:Exit")'),
    # --- liquidNames array ---
    ('"水", "粘稠", "有机", "自定义"', '"Water", "Thick", "Organic", "Custom"'),
]

for old, new in replacements:
    content = content.replace(old, new)

# ===== 4. Handle drawCN_cur(s) and drawCN_cur(liquidNames[...]) =====
# These are variable-based calls that pass ASCII-safe strings
content = content.replace('drawCN_cur(s);', 'u8g2.print(s);')
content = content.replace('drawCN_cur(liquidNames[currentLiquid]);', 'u8g2.print(liquidNames[currentLiquid]);')

# ===== 5. Handle remaining drawCN_cur calls (catch any missed) =====
# Replace any remaining drawCN_cur(...) with u8g2.print(...)
content = re.sub(r'drawCN_cur\(([^)]+)\)', r'u8g2.print(\1)', content)

# ===== 6. Remove the drawCN function if still present =====
if 'int drawCN(int x, int y, const char* text)' in content:
    # Find and remove the function
    content = re.sub(
        r'int drawCN\(int x, int y, const char\* text\) \{.*?\n\}',
        '',
        content,
        flags=re.DOTALL
    )

# ===== 7. Clean up the setup() RAM buffer init =====
content = content.replace(
    """  // Step 3.5: 字库 RAM 副本 (解决 Flash 大数组访问 + MSB/LSB 位序翻转)
  int bmBytes = cnTotalBytes();
  ram_bm = (uint8_t*)malloc(bmBytes);
  if (ram_bm) {
    memcpy(ram_bm, pump_chinese_font_bitmaps, bmBytes);
    Serial.printf("[SETUP] font ram %d bytes ok\\n", bmBytes);
  } else {
    Serial.println("[SETUP] font ram MALLOC FAILED!");
  }""",
    ""
)

# Write output
with open(SRC, "w", encoding="utf-8") as f:
    f.write(content)

print("Done. All Chinese removed, replaced with English.")
print("Verify the file and compile.")
