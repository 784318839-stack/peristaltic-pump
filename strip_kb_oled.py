#!/usr/bin/env python3
"""Comment out all keyboard and OLED code from peristaltic_pump.ino and pump_shared.h"""

import re

# ===== 1. Process peristaltic_pump.ino =====
ino_path = r"C:\Users\xg821\peristaltic_pump\peristaltic_pump.ino"
with open(ino_path, 'r', encoding='utf-8') as f:
    content = f.read()

# Comment out includes
content = content.replace(
    '#include <U8g2lib.h>      // OLED',
    '// #include <U8g2lib.h>      // OLED (disabled)')
content = content.replace(
    '#include <Keypad.h>        // 4×4 矩阵键盘',
    '// #include <Keypad.h>        // 4×4 矩阵键盘 (disabled)')

# Comment out I2C pin defines
content = content.replace(
    '#define I2C_SDA    21                // OLED I2C 数据线\n#define I2C_SCL    7                 // OLED I2C 时钟线',
    '// #define I2C_SDA    21                // OLED I2C 数据线 (disabled)\n// #define I2C_SCL    7                 // OLED I2C 时钟线 (disabled)')

# Comment out u8g2 object
content = content.replace(
    'U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, I2C_SCL, I2C_SDA);',
    '// U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, I2C_SCL, I2C_SDA); // OLED disabled')

# Comment out keypad pin definitions + keymap + Keypad object
content = re.sub(
    r'(const byte ROWS = 4, COLS = 4;\n)'
    r'(byte rowPins\[ROWS\] = \{.*?\};\s*// 行输出\n)'
    r'(byte colPins\[COLS\] = \{.*?\};\s*// 列输入\n)'
    r'(char keys\[ROWS\]\[COLS\] = \{\n\s*\{.*?\},\n\s*\{.*?\},\n\s*\{.*?\},\n\s*\{.*?\}\n\s*\};)\n'
    r'(Keypad keypad = Keypad\(makeKeymap\(keys\), rowPins, colPins, ROWS, COLS\);)',
    r'// Keypad disabled:\n// \1// \2// \3// \4\n// \5',
    content, flags=re.DOTALL)

# Comment out SCREENSAVER_MS
content = content.replace(
    '#define SCREENSAVER_MS    120000     // 2 分钟无按键 OLED 全黑休眠',
    '// #define SCREENSAVER_MS    120000     // 2 分钟无按键 OLED 全黑休眠 (disabled)')

# Comment out screensaver state vars
content = content.replace(
    'unsigned long lastUserActivity = 0;      // 最后一次用户按键的时间戳\nbool          screensaverActive = false; // 屏保是否激活',
    '// unsigned long lastUserActivity = 0;      // 最后一次用户按键的时间戳 (disabled)\n// bool          screensaverActive = false; // 屏保是否激活 (disabled)')

# Comment out ALL draw functions and updateDisplay
# Find the draw function section and key handler section
lines = content.split('\n')
new_lines = []
in_draw_section = False
in_key_section = False
skip_mode = False

for i, line in enumerate(lines):
    # Start of drawing section
    if '//                       OLED 界面绘制函数' in line:
        in_draw_section = True
        new_lines.append('// ===== OLED & Keypad DISABLED =====')
        new_lines.append('// All drawing and key handling functions commented out.')
        new_lines.append('// Remote control via WiFi/BLE/Serial only.')
        new_lines.append('')
        skip_mode = True
        continue

    # End of draw section, start of numeric input
    if in_draw_section and '数字输入处理' in line:
        # Keep in skip mode until end of key handlers
        continue

    # Start of key handlers section
    if '键盘顶层分发' in line or '// 根据 currentMenu' in line:
        in_key_section = True
        continue

    # End of key handlers (line before "// 蜂鸣器音效" or similar)
    if (in_draw_section or in_key_section) and ('// 屏幕总调度' in line or '蜂鸣器' in line):
        in_draw_section = False
        in_key_section = False
        skip_mode = False
        new_lines.append('')
        new_lines.append('// ===== End of disabled section =====')
        new_lines.append('')
        new_lines.append(line)
        continue

    if skip_mode:
        # Comment out each line
        stripped = line.rstrip()
        if stripped and not stripped.startswith('//') and not stripped.startswith('/*'):
            new_lines.append('// ' + line)
        elif stripped:
            new_lines.append(line)
        else:
            new_lines.append(line)
        continue

    # Setup: comment out OLED init
    if 'u8g2.begin();' in line and '//' not in line.split('u8g2')[0].strip():
        new_lines.append('  // u8g2.begin(); // OLED disabled')
        continue
    if 'pinMode(I2C_SDA, INPUT_PULLUP);' in line:
        new_lines.append('  // pinMode(I2C_SDA, INPUT_PULLUP); // OLED disabled')
        continue
    if 'pinMode(I2C_SCL, INPUT_PULLUP);' in line:
        new_lines.append('  // pinMode(I2C_SCL, INPUT_PULLUP); // OLED disabled')
        continue
    if 'Serial.println("[SETUP] oled ok");' in line:
        new_lines.append('  // Serial.println("[SETUP] oled ok"); // OLED disabled')
        continue

    # Setup: comment out lastUserActivity init
    if 'lastUserActivity = millis();' in line and '//' not in line:
        new_lines.append('  // lastUserActivity = millis(); // keypad disabled')
        continue

    # Loop: comment out key scan block
    if 'keypad.getKey()' in line:
        new_lines.append('  // --- Keypad disabled ---')
        new_lines.append('  // char key = keypad.getKey();')
        new_lines.append('  // if (key) {')
        new_lines.append('  //   Serial.print("[KEY] "); Serial.println(key);')
        new_lines.append('  //   handleKey(key);')
        new_lines.append('  //   lastUserActivity = millis();')
        new_lines.append('  //   if (screensaverActive) screensaverActive = false;')
        new_lines.append('  // }')
        # Skip the next ~7 lines (the key scan block)
        skip_mode = True
        skip_count = 7
        continue

    if skip_mode and skip_count > 0:
        skip_count -= 1
        if skip_count == 0:
            skip_mode = False
        continue

    # Loop: comment out screensaver check
    if 'screensaverActive' in line and 'millis()' in line and 'SCREENSAVER_MS' in line:
        new_lines.append('  // ' + line.strip() + ' // screensaver disabled')
        continue
    if 'screensaverActive = true;' in line and '//' not in line.split('=')[0]:
        new_lines.append('  // ' + line.strip() + ' // screensaver disabled')
        continue

    # Loop: comment out updateDisplay call
    if 'updateDisplay();' in line and '//' not in line:
        new_lines.append('  // updateDisplay(); // OLED disabled')
        continue

    new_lines.append(line)

content = '\n'.join(new_lines)

with open(ino_path, 'w', encoding='utf-8') as f:
    f.write(content)

print("Processed peristaltic_pump.ino")

# ===== 2. Process pump_shared.h =====
shared_path = r"C:\Users\xg821\peristaltic_pump\pump_shared.h"
with open(shared_path, 'r', encoding='utf-8') as f:
    content = f.read()

# Comment out U8g2/Keypad includes
content = content.replace(
    '#include <U8g2lib.h>',
    '// #include <U8g2lib.h> // OLED disabled')
content = content.replace(
    '#include <Keypad.h>',
    '// #include <Keypad.h> // keypad disabled')

# Comment out extern declarations
content = content.replace(
    'extern unsigned long lastUserActivity;',
    '// extern unsigned long lastUserActivity; // disabled')
content = content.replace(
    'extern bool         screensaverActive;',
    '// extern bool         screensaverActive; // disabled')
content = content.replace(
    'extern U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2;',
    '// extern U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2; // OLED disabled')
content = content.replace(
    'extern Keypad keypad;',
    '// extern Keypad keypad; // keypad disabled')
content = content.replace(
    'void updateDisplay();',
    '// void updateDisplay(); // OLED disabled')
content = content.replace(
    'void handleKey(char key);',
    '// void handleKey(char key); // keypad disabled')

with open(shared_path, 'w', encoding='utf-8') as f:
    f.write(content)

print("Processed pump_shared.h")

# ===== 3. Sync to Desktop =====
import shutil
shutil.copy(ino_path, r"C:\Users\xg821\Desktop\peristaltic_pump\peristaltic_pump.ino")
shutil.copy(shared_path, r"C:\Users\xg821\Desktop\peristaltic_pump\pump_shared.h")
print("Synced to Desktop backup")
print("\nDone! Compile and test.")
