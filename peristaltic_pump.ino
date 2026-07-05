/******************************************************************************
 * 蠕动泵控制器 - 完整版
 *
 * 硬件平台: ESP32
 * 驱动库:   U8g2 (OLED), Keypad (矩阵键盘), AccelStepper (步进电机)
 *
 * 功能:
 *   VOL模式 - 设定流量 + 目标VOL, 恒速运行自动停止
 *   时间模式 - 设定VOL + 时间, 自动计算流量, TIMED定量
 *   JET模式 - 设定单次量 + 间隔, 循环JET, 适配粘稠液体
 *   暂停/恢复 - 运行中暂停, 从断点继续
 *   校准向导 - 4 步引导校准 steps/mL
 *   Advanced - 回吸量 / 管路寿命
 *   预灌快排 - 全速运转排空/冲洗
 *   防滴回吸 - 完成后反转吸回余液
 *   匀加速   - 0.5 秒缓启动/缓停止, 无水锤
 *   自动关使能 - 待机 5 秒后电机线圈断电
 *   OLED 屏保 - 2 分钟无操作全黑休眠
 *   蜂鸣反馈 - 按键 / 确认 / 取消 / 启停 / 完成各有不同音效
 *   EEPROM 掉电记忆 - 所有参数 + 累计总量 + 管路寿命
 *   管路寿命提醒 - 超过 80% 时告警
 ******************************************************************************/

#include <Arduino.h>
// #include <U8g2lib.h>      // OLED (disabled) 图形库 - SH1106 128×64 I2C
// #include <Keypad.h>        // 4×4 矩阵键盘 (disabled)
#include <AccelStepper.h>  // 步进电机 - 匀加速/多速度模式
#include <EEPROM.h>        // ESP32 Flash 模拟 EEPROM - 掉电保存参数

// ----- 远程控制模块 (WiFi + WebSocket + USB 串口) -----
#include "command_protocol.h"
#include "serial_commands.h"
#include "wifi_manager.h"
#include "web_handlers.h"
#include "bluetooth_manager.h"

// ============================================================================
//                              引脚 & 常量定义
// ============================================================================

// ----- EEPROM 布局 -----
// Magic 值: 每次改 EEPROM 布局时 +1, 旧数据检测到版本不匹配则用默认值
#define EEPROM_MAGIC  0x5058        // "PX" = Pump V4 (+多液体校准)
#define EEPROM_ADDR   0             // 起始地址

// ----- 硬件引脚 (ESP32 GPIO) -----
#define STEP_PIN   16               // 步进电机 - 脉冲
#define DIR_PIN    17               // 步进电机 - 方向
#define ENA_PIN    18               // 步进电机 - 使能 (低电平有效)
#define BUZZER_PIN 1                // 有源蜂鸣器 - 用 digitalWrite() 驱动
// #define I2C_SDA    21                // OLED I2C 数据线 (disabled)
// #define I2C_SCL    7                 // OLED I2C 时钟线 (disabled)

// 步进电机每转步数 (驱动器微步设置为 8 细分时 = 200×8 = 1600)
// 此值仅用于参考, 实际用 stepsPerMl 计算
const int STEPS_PER_REV = 1600;

// ----- 硬件对象 -----
// AccelStepper::DRIVER - 只控制 STEP+DIR, 无反馈
AccelStepper stepper(AccelStepper::DRIVER, STEP_PIN, DIR_PIN);

// SH1106 OLED, 128×64 像素, 硬件 I2C (GPIO21=SDA, GPIO7=SCL)
// U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, I2C_SCL, I2C_SDA); // OLED disabled

// ----- 4×4 矩阵键盘 -----
// Keypad disabled:
// const byte ROWS = 4, COLS = 4;
// byte rowPins[ROWS] = {4, 5, 13, 42};     // 行输出
// byte colPins[COLS] = {38, 39, 40, 47};   // 列输入
// char keys[ROWS][COLS] = {
//   {'4','5','B','6'},
//   {'1','2','A','3'},
//   {'7','8','C','9'},
//   {'*','0','D','#'}
// };
// Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// ----- 空闲超时 ----
#define IDLE_DISABLE_MS   5000       // 待机/暂停后 5 秒自动关电机使能
// #define SCREENSAVER_MS    120000     // 2 分钟无按键 OLED 全黑休眠 (disabled)
#define COMPLETIONS_PER_SAVE 10      // 每完成 10 次泵送写一次 EEPROM 累计量

// ============================================================================
//                              全局参数
// ============================================================================

// ----- 泵送核心参数 -----
float stepsPerMl      = 1000.0;      // 校准值: 每 mL 需要多少步 (范围 10~50000)
float flowRate        = 50.0;        // 当前流量 mL/min (范围 0.1~2000)
float targetVolume    = 10.0;        // 目标VOL mL (范围 0.1~99999)
float dispensedVolume = 0;           // 本次已打出VOL mL (实时更新)

// ----- 运行模式 -----
enum PumpMode { MODE_VOLUME, MODE_TIME, MODE_JET };
PumpMode pumpMode = MODE_VOLUME;     // D 键三态循环: VOL->TIMED->JET

// ----- 时间模式参数 -----
// 时间模式下: flowRate 由 targetVolume / targetTime 自动计算
float targetTime      = 30.0;        // 目标时间 秒 (范围 1~86400 = 24h)
unsigned long pumpStartMs   = 0;     // 本次泵送启动时刻 millis()
unsigned long pumpElapsed   = 0;     // 已运行秒数 (实时更新)
unsigned long pumpDuration  = 0;     // 总TIMED秒数 (启动时从 targetTime 复制)

// ----- JET模式参数 -----
float jetVolume    = 1.0;            // Shot Vol mL (0.1~10)
float jetInterval  = 3.0;            // JET间隔 秒 (1~60)
float jetFlowRate  = 200.0;          // Jet Flow mL/min (10~2000)
float jetPressure  = 5.0;            // JET压力 1~10 档 (控制加速度/爆发力)
int   jetCount     = 0;              // 本次已JET次数
bool  jetSquirting = false;          // 是否正在喷出 (vs 等待间隔)
unsigned long jetWaitStart = 0;      // 等待阶段起始时刻 millis()

// ----- 多液体校准 -----
#define NUM_LIQUIDS 4
const char* liquidNames[NUM_LIQUIDS] = {"Wtr", "Thk", "Org", "Cst"};
float liquidSPM[NUM_LIQUIDS] = {1000.0, 1000.0, 1000.0, 1000.0}; // 每种液体的 stepsPerMl
int   currentLiquid = 0;             // 当前选中液体索引 (0=水 1=粘稠 2=有机 3=自定义)

// ----- 累计 & 管路寿命 -----
float antiDripVol     = 0.05;        // 防滴回吸量 mL (0=关闭, 最大 5.0)
float totalDispensed  = 0;           // 设备累计打出总量 mL (存入 EEPROM)
float tubeLifeML      = 50000;       // 管路寿命 mL (0=disabled更换)
int   completionCount = 0;           // 完成计数器, 凑够 COMPLETIONS_PER_SAVE 写 EEPROM

// ============================================================================
//                              状态机
// ============================================================================

// ----- 泵运行状态 -----
// STATE_IDLE     : 待机, 可随时启动
// RUNNING  : 电机在转, 正向泵出
// PAUSED   : 运行中暂停, 保存断点, 可从断点恢复
// DONE     : 到达目标VOL/时间, 等待复位
// ANTI_DRIP: 正在执行回吸 (反转一小段, 不可打断)
enum State { STATE_IDLE, RUNNING, PAUSED, DONE, ANTI_DRIP };
State pumpState = STATE_IDLE;

// ----- 暂停断点 (从暂停恢复时用到) -----
long          pausedRemainingSteps = 0;  // 剩余步数 (VOL模式和时间模式都用)
unsigned long pausedElapsedSec    = 0;  // 已过秒数 (仅时间模式用)

// ----- 当前菜单 -----
// MAIN      : 主界面 (显示模式/状态/进度)
// SET_FLOW  : 设置流量
// SET_VOL   : 设置VOL
// SET_TIME  : 设置时间
// CALIBRATE : 校准向导 (含 5 个子步骤)
// PRIME     : PRIME/PURGE
enum Menu { MAIN, SET_FLOW, SET_VOL, SET_TIME, CALIBRATE, PRIME, SET_JET_VOL, SET_JET_INTERVAL, SET_JET_FLOW, SET_JET_PRESSURE, SELECT_LIQUID, JET_OPTIONS, PRESET_LOAD };
Menu currentMenu = MAIN;

// ----- 校准向导子状态 (currentMenu == CALIBRATE 时有效) -----
// CALIB_SET_VOL  : 1/4 设定校准的目标VOL
// CALIB_RUN      : 2/4 运行泵 + 实时进度
// CALIB_MEASURE  : 3/4 用量筒实测, 输入实际VOL
// CALIB_RESULT   : 4/4 显示新旧值对比, 确认或放弃
// CALIB_SETTINGS : 5/5 Advanced (回吸量 / 管路寿命)
enum CalibStep { CALIB_IDLE, CALIB_SELECT_LIQUID, CALIB_SET_VOL, CALIB_RUN, CALIB_MEASURE, CALIB_RESULT, CALIB_SETTINGS };
CalibStep calibStep = CALIB_IDLE;

// Advanced中的编辑光标: 正在编辑哪个参数
enum SetEdit { SET_NONE, SET_ANTI_DRIP, SET_TUBE_LIFE, SET_LIQUID };
SetEdit settingEdit = SET_NONE;

// 校准过程使用的临时变量
float calibTargetVol = 10.0;         // 校准用的目标VOL
float calibActualVol = 0;            // 量筒实测得到的实际VOL
long  calibStepsRun  = 0;            // 校准运行中实际走了多少步
float calibNewSPM    = 0;            // 计算得到的新 stepsPerMl
bool  calibRunning   = false;        // 校准泵是否正在转 (有别于正常 pumpState)

// ----- 自动关使能状态 -----
bool         stepperEnabled      = true;   // 当前电机使能状态
unsigned long lastStepperActivity = 0;     // 最后一次电机活动的时间戳

// ----- EEPROM 脏标记 -----
// 参数修改时 markDirty(), 空闲/完成时 saveParams() 统一写入
// 避免频繁擦写 Flash (ESP32 Flash 寿命 ~10 万次擦除)
bool eepromDirty = false;

// ----- 方案预设 -----
int presetSlot = 0;                  // 当前正在查看的方案号 (0~3)

// ----- 蜂鸣器 & 屏保状态 -----
State        prevPumpState     = STATE_IDLE;   // 上一帧泵状态 (用于检测 DONE 跳变)
// unsigned long lastUserActivity = 0;      // 最后一次用户按键的时间戳 (disabled)
// bool          screensaverActive = false; // 屏保是否激活 (disabled)

// ============================================================================
//                              蜂鸣器
// ============================================================================
// 使用有源蜂鸣器 + digitalWrite() 驱动
// 有源蜂鸣器自带振荡电路, 通电即响, 无需 PWM; 所有音效阻塞式 (最长 ~310ms)

// 底层: 通电指TIMED长
void buzz(int freq, int ms) {
  (void)freq;                     // 有源蜂鸣器不可调频, 保留参数兼容
  digitalWrite(BUZZER_PIN, HIGH); // 通电鸣响
  delay(ms);                      // 等音效播完
  digitalWrite(BUZZER_PIN, LOW);  // 断电
}

// 每个按键输入 - 12ms 短促 1200Hz 高音, 几乎不被察觉
void beepInput()   { buzz(1200, 12); }

// 确认 (* 键保存) - 80ms 880Hz 中音
void beepConfirm() { buzz(880, 80);  }

// 取消/返回 (D 键) - 80ms 440Hz 低音
void beepCancel()  { buzz(440, 80);  }

// 启动/恢复 - 上行双音 (660->880Hz), 听感 "嘀~叮↗"
void beepStart()   { buzz(660, 50); delay(35); buzz(880, 80); }

// 暂停 - 下行双音 (880->660Hz), 听感 "叮~嘀↘"
void beepPause()   { buzz(880, 50); delay(35); buzz(660, 80); }

// 完成 - 三连音 1000Hz, 听感 "叮·叮·叮--"
void beepDone()    { buzz(1000, 60); delay(45); buzz(1000, 60); delay(45); buzz(1000, 100); }

// 电机自动断电 - 80ms 200Hz 低沉音, 听感 "嗡"
void beepDisable() { buzz(200, 80);  }

// ============================================================================
//                           数字输入缓冲
// ============================================================================
// 所有参数输入界面共用这一个缓冲区和三个函数
// 最多输入 6 个字符 (含小数点), 支持退格, 转 float

char inputBuf[8] = "";           // 输入缓冲区 (含 '\0')
int  inputLen    = 0;            // 当前已输入字符数
bool inputActive = false;        // 保留字段 (当前未使用)

// 清空缓冲区 (进入输入界面时调用)
void inputClear() {
  memset(inputBuf, 0, sizeof(inputBuf));
  inputLen = 0;
}

// 退格 (C 键在输入模式下)
void inputBackspace() {
  if (inputLen > 0) {
    inputBuf[--inputLen] = 0;
  }
}

// 追加一个字符 (最多 6 个)
void inputAppend(char c) {
  if (inputLen < 6) {
    inputBuf[inputLen++] = c;
  }
}

// 将缓冲区内容转为 float (空缓冲区返回 0)
float inputToFloat() {
  if (inputLen == 0) return 0;
  return atof(inputBuf);
}

// ============================================================================
//                              泵控制核心
// ============================================================================

// 流量 mL/min -> 步进脉冲频率 step/s
// 公式: steps/s = mL/min × steps/mL ÷ 60 s/min
float flowRateToPPS(float mLmin) {
  return mLmin * stepsPerMl / 60.0;
}

// ----- 启动泵 -----
// 根据当前模式设置参数, 使能电机, 开始匀加速运行
void startPump() {
  // 时间Mode: 根据目标VOL和目标时间自动计算所需流量
  if (pumpMode == MODE_TIME) {
    if (targetVolume <= 0 || targetTime <= 0) return;
    float calcFlow = targetVolume / (targetTime / 60.0);  // mL/min
    flowRate = constrain(calcFlow, 0.1, 2000.0);          // 限制在硬件能力内
  } else {
    // VOLMode: 使用手动设定的流量
    if (flowRate <= 0 || targetVolume <= 0) return;
  }

  ensureStepperOn();               // 确保使能 (可能已被自动关闭)
  updateStepperSpeed();            // 根据当前 flowRate 计算并设速度/加速度

  pumpDuration = (unsigned long)targetTime;  // 时间模式下用于超时保护
  pumpElapsed  = 0;
  pumpStartMs  = millis();         // 记录启动时刻 (时间模式计时用)
  dispensedVolume = 0;

  // 计算需要走的总步数 = 目标VOL × 每 mL 步数
  long totalSteps = targetVolume * stepsPerMl;
  stepper.setCurrentPosition(0);   // 清零软件坐标
  stepper.moveTo(totalSteps);      // 设定目标位置 (AccelStepper 会自动加减速)

  beepStart();                     // 启动音 - 上行双音
  pumpState = RUNNING;
}

// ----- 停止泵 (回到待机) -----
void stopPump() {
  stepper.stop();                  // 立即停止脉冲输出 (不减速)
  pumpState = STATE_IDLE;
}

// ----- 暂停泵 -----
// 保存断点 (剩余步数 + 已过时间), 停止脉冲
// 暂停后电机仍在使能状态, 5 秒后由 checkIdleDisable() 自动关
void pausePump() {
  stepper.stop();                  // 停止脉冲, 软件位置不变
  pausedRemainingSteps = stepper.distanceToGo();  // 保存剩余步数 (两种模式都用)
  if (pumpMode == MODE_TIME) {
    pausedElapsedSec = (millis() - pumpStartMs) / 1000;  // 保存已过秒数
  }
  beepPause();                     // 暂停音 - 下行双音
  pumpState = PAUSED;
}

// ----- 从暂停恢复 -----
// 恢复使能 (如果已被自动关), 重算速度, 从断点继续
// VOLMode: 直接从剩余步数继续 (不重置坐标, dispensedVolume 不归零)
// 时间Mode: 调整计时起点使 pumpElapsed 从断点续计
void resumePump() {
  ensureStepperOn();               // 可能在暂停期间被自动关使能了
  updateStepperSpeed();            // 重新设速度 (flowRate 可能被改了)
  beepStart();                     // 恢复音 = 启动音
  if (pumpMode == MODE_TIME) {
    // 把 startMs 往前拨, 让 pumpElapsed 从 pausedElapsedSec 继续
    pumpStartMs = millis() - pausedElapsedSec * 1000;
  }
  // 关键: 不 reset position, 直接设目标 = 当前位置 + 剩余步数
  // 这样 dispensedVolume = currentPosition / stepsPerMl 不会归零
  stepper.moveTo(stepper.currentPosition() + pausedRemainingSteps);
  pumpState = RUNNING;
}

// ----- 复位泵 -----
// 清零所有计数, 回到待机状态
void resetPump() {
  dispensedVolume = 0;
  pumpElapsed     = 0;
  pausedRemainingSteps = 0;
  pausedElapsedSec     = 0;
  stepper.setCurrentPosition(0);   // 软件坐标归零
  pumpState = STATE_IDLE;
}

// ----- 更新步进速度/加速度 (流量改变后必须调用) -----
// 速度曲线: 从 0 匀加速到目标速度 (约 0.5 秒), 巡航, 到目标时匀减速到 0
// AccelStepper.run() 函数自动处理梯形速度曲线
void updateStepperSpeed() {
  float pps = flowRateToPPS(flowRate);  // 目标脉冲频率 step/s
  stepper.setMaxSpeed(pps);             // 巡航速度 = 目标速度 (不超速)
  stepper.setSpeed(0);                  // 起始速度 = 0 (从零加速)
  stepper.setAcceleration(pps * 2);     // 加速度 = 2×目标速度 -> ~0.5 秒达全速
}

// ----- JETMode: 单次喷出 -----
// jetFlowRate 控制速度, jetPressure 控制加速度/爆发力
void startJetSquirt() {
  ensureStepperOn();               // 间隔中可能已被自动断电
  float pps = jetFlowRate * stepsPerMl / 60.0;
  stepper.setMaxSpeed(pps);
  stepper.setSpeed(0);
  // 加速度 = pps × (压力档位 × 0.4), 范围 0.4x~4.0x
  // 1=柔和(0.4x)  5=标准(2.0x)  10=猛冲(4.0x)
  stepper.setAcceleration(pps * jetPressure * 0.4);
  stepper.setCurrentPosition(0);
  long jetSteps = jetVolume * stepsPerMl;
  if (jetSteps < 1) jetSteps = 1;              // 最少 1 步
  stepper.moveTo(jetSteps);
  jetSquirting = true;
}

// ----- JETMode: 开始循环 (立即喷第一下) -----
void startJetCycle() {
  jetCount = 0;
  dispensedVolume = 0;
  stepper.setCurrentPosition(0);
  ensureStepperOn();
  startJetSquirt();
  beepStart();
  pumpState = RUNNING;
}

// ----- JETMode: 停止循环 -----
void stopJetCycle() {
  stepper.stop();
  jetSquirting = false;
  pumpState = STATE_IDLE;
}

// ----- 切换当前Liq: 同步 stepsPerMl = liquidSPM[idx] -----
void selectLiquid(int idx) {
  if (idx < 0 || idx >= NUM_LIQUIDS) return;
  currentLiquid = idx;
  stepsPerMl = liquidSPM[idx];
  markDirty();
}

// ----- 方案预设 (4 槽位, EEPROM offset 64 起, 每槽 30 字节) -----
#define PRESET_BASE   64
#define PRESET_SIZE   30

bool isPresetValid(int slot) {
  if (slot < 0 || slot > 3) return false;
  uint8_t mode = EEPROM.read(PRESET_BASE + slot * PRESET_SIZE);
  return (mode <= 2);  // 0=VOL 1=TIMED 2=JET 均有效
}

void savePreset(int slot) {
  if (slot < 0 || slot > 3) return;
  int base = PRESET_BASE + slot * PRESET_SIZE;
  EEPROM.put(base,      (uint8_t)pumpMode);
  EEPROM.put(base + 1,  (uint8_t)currentLiquid);
  EEPROM.put(base + 2,  flowRate);
  EEPROM.put(base + 6,  targetVolume);
  EEPROM.put(base + 10, targetTime);
  EEPROM.put(base + 14, jetVolume);
  EEPROM.put(base + 18, jetInterval);
  EEPROM.put(base + 22, jetFlowRate);
  EEPROM.put(base + 26, jetPressure);
  EEPROM.commit();
}

void loadPreset(int slot) {
  if (!isPresetValid(slot)) return;
  int base = PRESET_BASE + slot * PRESET_SIZE;
  pumpMode      = (PumpMode)EEPROM.read(base);
  currentLiquid = EEPROM.read(base + 1);
  EEPROM.get(base + 2,  flowRate);
  EEPROM.get(base + 6,  targetVolume);
  EEPROM.get(base + 10, targetTime);
  EEPROM.get(base + 14, jetVolume);
  EEPROM.get(base + 18, jetInterval);
  EEPROM.get(base + 22, jetFlowRate);
  EEPROM.get(base + 26, jetPressure);
  // 约束 + 同步
  flowRate     = constrain(flowRate,     0.1, 2000.0);
  targetVolume = constrain(targetVolume, 0.1, 99999);
  targetTime   = constrain(targetTime,   1, 86400);
  jetVolume    = constrain(jetVolume,    0.1, 10.0);
  jetInterval  = constrain(jetInterval,  1, 60);
  jetFlowRate  = constrain(jetFlowRate,  10, 2000.0);
  jetPressure  = constrain(jetPressure,  1, 10);
  stepsPerMl   = liquidSPM[currentLiquid];
  markDirty();
  saveParams();
  resetPump();
  jetCount = 0;
}

// ============================================================================
//                          EEPROM 掉电保存
// ============================================================================
// ESP32 的 EEPROM 其实是 Flash 模拟, 每次 commit() 擦写整个扇区
// 寿命 ~10 万次擦除 -> 必须用脏标记减少写入频率
//
// EEPROM 内存布局 (512 字节, begin(512)):
//   核心参数 (0-63, 64B):
//     Offset  0: Magic 校验字       (2B) - 0x5058
//     Offset  2: stepsPerMl         (4B) - float (运行时 = liquidSPM[currentLiquid])
//     Offset  6: flowRate           (4B) - float
//     Offset 10: targetVolume       (4B) - float
//     Offset 14: targetTime         (4B) - float
//     Offset 18: pumpMode           (1B) - uint8_t
//     Offset 19: antiDripVol        (4B) - float
//     Offset 23: totalDispensed     (4B) - float
//     Offset 27: tubeLifeML         (4B) - float
//     Offset 31: jetVolume          (4B) - float
//     Offset 35: jetInterval        (4B) - float
//     Offset 39: jetFlowRate        (4B) - float
//     Offset 43-58: liquidSPM[4]    (16B) - float×4
//     Offset 59: currentLiquid      (1B) - uint8_t
//     Offset 60: jetPressure        (4B) - float
//   方案预设 (64-183, 120B): 4槽 × 30B
//   WiFi 配置 (184-283, 100B):
//     Offset 184: WiFi Magic        (2B) - 0x5746
//     Offset 186: SSID              (32B)
//     Offset 218: Password          (64B)
//     Offset 282: WiFi Mode         (1B)
//   预留 (284-511, 228B)

// 标记"有参数改了, 需要写"
void markDirty() { eepromDirty = true; }

// 实际写入 Flash (仅在 eepromDirty==true 时执行)
void saveParams() {
  if (!eepromDirty) return;        // 没改动, 跳过
  EEPROM.put(EEPROM_ADDR,     (uint16_t)EEPROM_MAGIC);
  EEPROM.put(EEPROM_ADDR + 2, stepsPerMl);
  EEPROM.put(EEPROM_ADDR + 6, flowRate);
  EEPROM.put(EEPROM_ADDR + 10, targetVolume);
  EEPROM.put(EEPROM_ADDR + 14, targetTime);
  EEPROM.put(EEPROM_ADDR + 18, (uint8_t)pumpMode);
  EEPROM.put(EEPROM_ADDR + 19, antiDripVol);
  EEPROM.put(EEPROM_ADDR + 23, totalDispensed);
  EEPROM.put(EEPROM_ADDR + 27, tubeLifeML);
  EEPROM.put(EEPROM_ADDR + 31, jetVolume);
  EEPROM.put(EEPROM_ADDR + 35, jetInterval);
  EEPROM.put(EEPROM_ADDR + 39, jetFlowRate);
  for (int i = 0; i < NUM_LIQUIDS; i++)
    EEPROM.put(EEPROM_ADDR + 43 + i * 4, liquidSPM[i]);
  EEPROM.put(EEPROM_ADDR + 59, (uint8_t)currentLiquid);
  EEPROM.put(EEPROM_ADDR + 60, jetPressure);
  EEPROM.commit();                 // 真正擦写 Flash
  eepromDirty = false;
}

// 从 Flash 读取参数, 返回 false 表示首次上电 (Magic 不匹配)
bool loadParams() {
  uint16_t magic;
  EEPROM.get(EEPROM_ADDR, magic);
  if (magic != EEPROM_MAGIC) return false;  // 版本不匹配 -> 用默认值

  EEPROM.get(EEPROM_ADDR + 2,  stepsPerMl);
  EEPROM.get(EEPROM_ADDR + 6,  flowRate);
  EEPROM.get(EEPROM_ADDR + 10, targetVolume);
  EEPROM.get(EEPROM_ADDR + 14, targetTime);
  pumpMode     = (PumpMode)EEPROM.read(EEPROM_ADDR + 18);
  if (pumpMode > MODE_JET) pumpMode = MODE_VOLUME; // 防越界
  EEPROM.get(EEPROM_ADDR + 19, antiDripVol);
  EEPROM.get(EEPROM_ADDR + 23, totalDispensed);
  EEPROM.get(EEPROM_ADDR + 27, tubeLifeML);
  EEPROM.get(EEPROM_ADDR + 31, jetVolume);
  EEPROM.get(EEPROM_ADDR + 35, jetInterval);
  EEPROM.get(EEPROM_ADDR + 39, jetFlowRate);
  for (int i = 0; i < NUM_LIQUIDS; i++)
    EEPROM.get(EEPROM_ADDR + 43 + i * 4, liquidSPM[i]);
  currentLiquid = EEPROM.read(EEPROM_ADDR + 59);
  if (currentLiquid >= NUM_LIQUIDS) currentLiquid = 0;
  EEPROM.get(EEPROM_ADDR + 60, jetPressure);

  // 防止 Flash 中的数据被意外改写导致越界值
  stepsPerMl    = constrain(stepsPerMl,    10, 50000);
  flowRate      = constrain(flowRate,      0.1, 2000.0);
  targetVolume  = constrain(targetVolume,  0.1, 99999);
  targetTime    = constrain(targetTime,    1, 86400);
  antiDripVol   = constrain(antiDripVol,   0, 5.0);
  tubeLifeML    = constrain(tubeLifeML,    0, 200000);
  jetVolume     = constrain(jetVolume,     0.1, 10.0);
  jetInterval   = constrain(jetInterval,   1, 60);
  jetFlowRate   = constrain(jetFlowRate,   10, 2000.0);
  jetPressure   = constrain(jetPressure,   1, 10);
  for (int i = 0; i < NUM_LIQUIDS; i++)
    liquidSPM[i] = constrain(liquidSPM[i], 10, 50000);
  // 同步 stepsPerMl = 当前液体的校准值
  stepsPerMl = liquidSPM[currentLiquid];
  return true;
}

// ============================================================================
//                            使能管理
// ============================================================================
// 步进电机在待机/暂停时若一直使能, 线圈持续通电会发热
// 策略: 停止 5 秒后自动 disableOutputs(), 下次运转前 enableOutputs()

// 确保电机使能打开 (启动/恢复前调用)
void ensureStepperOn() {
  if (!stepperEnabled) {
    stepper.enableOutputs();       // 重新通电
    stepperEnabled = true;
  }
  lastStepperActivity = millis();  // 重置空闲计时
}

// 每个 loop 周期检查: 空闲/暂停超过 IDLE_DISABLE_MS 则关使能
void checkIdleDisable() {
  if (stepperEnabled
      && (pumpState == STATE_IDLE || pumpState == PAUSED)  // 待机或暂停
      && millis() - lastStepperActivity > IDLE_DISABLE_MS) {
    stepper.disableOutputs();      // 线圈断电
    stepperEnabled = false;
    beepDisable();                 // 低沉提示音
  }
}

// ============================================================================
//                           校准向导逻辑
// ============================================================================
// 引导用户: 设定VOL -> 泵出液体 -> 用量筒实测 -> 自动计算 steps/mL -> Advanced
// Cal Result立即写入 EEPROM

// 校准: 启动泵 (用当前 stepsPerMl 跑 calibTargetVol)
void calibStartRun() {
  if (calibTargetVol <= 0) return;
  ensureStepperOn();
  updateStepperSpeed();
  dispensedVolume = 0;
  long totalSteps = calibTargetVol * stepsPerMl;  // 用旧校准值计算步数
  stepper.setCurrentPosition(0);
  stepper.moveTo(totalSteps);
  calibRunning = true;
  pumpState = RUNNING;
}

// 校准: 手动停止泵 (记录已走步数, 支持中途停止也能正确计算)
void calibStopRun() {
  calibStepsRun = stepper.currentPosition();  // 先记步数, 再停
  stepper.stop();
  calibRunning = false;
  pumpState = STATE_IDLE;
}

// 校准: 泵到达目标 (由 loop 检测 distanceToGo==0 后调用)
void calibFinishRun() {
  calibStepsRun = stepper.currentPosition();  // 记录实际走的步数
  calibRunning = false;
  pumpState = DONE;
}

// 校准: 根据实际步数和实测VOL计算新的 stepsPerMl
// 公式: newSPM = stepsRun / actualVol
// 例: 设 10mL, 走了 9500 步, 量筒实测 9.5mL -> newSPM = 9500/9.5 = 1000
void calibCalculate() {
  if (calibActualVol > 0 && calibStepsRun > 0) {
    calibNewSPM = (float)calibStepsRun / calibActualVol;
    calibNewSPM = constrain(calibNewSPM, 10, 50000);  // 防止离谱值
  }
}

// 校准: 保存新值到当前液体的 slot + EEPROM (用户明确按 * 确认)
void calibSave() {
  stepsPerMl = calibNewSPM;
  liquidSPM[currentLiquid] = calibNewSPM;
  markDirty();
  saveParams();                    // 立即写入 (用户确认操作)
}

// 校准: 初始化向导, 进入第 1 步
void calibEnter() {
  calibStep       = CALIB_SELECT_LIQUID;  // 第 1 步: 选择要校准的液体
  calibTargetVol  = 10.0;         // 默认用 10mL 校准
  calibActualVol  = 0;
  calibStepsRun   = 0;
  calibNewSPM     = 0;
  calibRunning    = false;
  inputClear();
  currentMenu     = CALIBRATE;
}

// ============================================================================
//                         OLED 屏幕绘制
// ============================================================================
// SH1106 128×64 像素
// 字体: u8g2_font_8x13_tr (标题), u8g2_font_6x10_tr (信息), u8g2_font_5x7_tr (按键提示)
// ============================================================================
//                         中文字体绘制
// ============================================================================
// ============================================================
// 用 clearBuffer / sendBuffer (兼容硬件和软件 I2C)

// ----- 主界面 (VOL / TIMED / JET) 14px 适配 -----
// void drawMain() {
//   u8g2.clearBuffer();
//     // 第一行: 模式标题 + 运行状态 (y=14, 适配 14px 中文)
//     u8g2.setFont(u8g2_font_8x13_tr);
//     u8g2.setCursor(0, 13);
//     switch (pumpMode) {
//       case MODE_TIME: u8g2.print("== TIMED"); break;
//       case MODE_JET:  u8g2.print("== JET"); break;
//       default:        u8g2.print("== VOL"); break;
//     }
//     u8g2.setFont(u8g2_font_6x10_tr);
//     u8g2.setCursor(110, 13);
//     u8g2.print(liquidNames[currentLiquid]);

//     const char* s;
//     bool tubeWarn = (tubeLifeML > 0 && totalDispensed > tubeLifeML * 0.8);
//     if (pumpMode == MODE_JET && pumpState == RUNNING) {
//       s = jetSquirting ? "Sqrt" : "Wait";
//     } else {
//       switch (pumpState) {
//         case STATE_IDLE:  s = tubeWarn ? "Idl!" : "Idle";   break;
//         case RUNNING:     s = "Run"; break;
//         case PAUSED:      s = "Pause"; break;
//         case ANTI_DRIP:   s = "Adrip"; break;
//         case DONE:        s = tubeWarn ? "Done!" : "Done"; break;
//       }
//     }
//     u8g2.setCursor(75, 13);
//     u8g2.print(s);

//     // ---- 时间模式 (4 行 + 进度条) ----
//     if (pumpMode == MODE_TIME) {
//       u8g2.setFont(u8g2_font_6x10_tr);
//       u8g2.setCursor(0, 24);
//       u8g2.print("Flow(auto):");
//       u8g2.print(flowRate, 1);
//       u8g2.print(" mL/min");

//       u8g2.setCursor(0, 37);
//       u8g2.print("Set:");
//       u8g2.print(targetVolume, 1);
//       u8g2.print("mL   T:");
//       u8g2.print((int)(targetTime / 60));
//       u8g2.print("m");
//       u8g2.print(fmod(targetTime, 60), 0);
//       u8g2.print("s");

//       u8g2.setCursor(0, 50);
//       u8g2.print("Out:");
//       u8g2.print(dispensedVolume, 1);
//       u8g2.print("mL   "); u8g2.print("Elap:");
//       u8g2.print((int)(pumpElapsed / 60));
//       u8g2.print("m");
//       u8g2.print(pumpElapsed % 60);
//       u8g2.print("s");

//       if (targetVolume > 0 && (pumpState == RUNNING || pumpState == PAUSED)) {
//         int barW = map(constrain(dispensedVolume * 100 / targetVolume, 0, 100), 0, 100, 0, 126);
//         u8g2.drawBox(0, 57, barW, 4);
//       }
//     }
//     // ---- JET 模式 (4 行 + 进度条) ----
//     else if (pumpMode == MODE_JET) {
//       u8g2.setFont(u8g2_font_6x10_tr);
//       u8g2.setCursor(0, 24);
//       u8g2.print("Shot:");
//       u8g2.print(jetVolume, 1);
//       u8g2.print("mL   Intv:");
//       u8g2.print((int)jetInterval);
//       u8g2.print("s");

//       u8g2.setCursor(0, 37);
//       u8g2.print("Flow:");
//       u8g2.print(jetFlowRate, 1);
//       u8g2.print(" Pres:");
//       u8g2.print((int)jetPressure);

//       u8g2.setCursor(0, 50);
//       u8g2.print("Shots:");
//       u8g2.print(jetCount);
//       u8g2.print("  ");
//       u8g2.print(dispensedVolume, 1);
//       u8g2.print("mL");

//       if (pumpState == RUNNING) {
//         if (jetSquirting) {
//           if (jetVolume > 0) {
//             long jetTotalSteps = jetVolume * stepsPerMl;
//             if (jetTotalSteps > 0) {
//               int barW = map(constrain((int)(stepper.currentPosition() * 100L / jetTotalSteps), 0, 100), 0, 100, 0, 126);
//               u8g2.drawBox(0, 57, barW, 4);
//             }
//           }
//         } else {
//           unsigned long remain = (jetInterval > 0) ? (jetInterval - (millis() - jetWaitStart) / 1000) : 0;
//           u8g2.setCursor(0, 50);
//           u8g2.print("Next:");
//           u8g2.print((int)remain);
//           u8g2.print("s");
//         }
//       }
//     }
//     // ---- VOL 模式 (4 行 + 进度条) ----
//     else {
//       u8g2.setFont(u8g2_font_8x13_tr);
//       u8g2.setCursor(0, 24);
//       u8g2.print("Flow:");
//       u8g2.print(flowRate, 1);
//       u8g2.print(" mL/min");

//       u8g2.setCursor(0, 37);
//       u8g2.print("Tgt:");
//       u8g2.print(targetVolume, 1);
//       u8g2.print(" mL");

//       u8g2.setCursor(0, 50);
//       u8g2.print("Out:");
//       u8g2.print(dispensedVolume, 1);
//       u8g2.print(" mL");

//       if (targetVolume > 0) {
//         int barW = map(constrain(dispensedVolume * 100 / targetVolume, 0, 100), 0, 100, 0, 126);
//         u8g2.drawBox(0, 57, barW, 4);
//       }
//     }

//     // ----- 底部小字 (y=63, 5x7 字体) -----
//     u8g2.setFont(u8g2_font_5x7_tr);
//     u8g2.setCursor(0, 62);
//     if ((millis() / 1000) % 5 < 3 || pumpState == RUNNING || pumpState == ANTI_DRIP) {
//       // 按键提示 - A/B 根据模式不同, C 根据状态不同, D 固定
//       if (pumpMode == MODE_TIME) {
//         u8g2.print("A:VOL B:TIME");
//       } else if (pumpMode == MODE_JET) {
//         u8g2.print("A:Shot B:Intv");
//       } else {
//         u8g2.print("A:Flow B:Vol");
//       }
//       switch (pumpState) {
//         case STATE_IDLE:      u8g2.print("C:Start");  break;
//         case RUNNING:   u8g2.print("C:Pause");  break;
//         case PAUSED:    u8g2.print("C:Resume");  break;
//         case ANTI_DRIP: u8g2.print("回吸中");  break;  // 回吸不可打断
//         case DONE:      u8g2.print("C:Reset");  break;
//       }
//       u8g2.print(" D:Mode");
//     } else {
//       // 统计信息轮播
//       u8g2.print("Total:");
//       u8g2.print((int)totalDispensed);
//       u8g2.print("mL");
//       if (tubeLifeML > 0) {
//         u8g2.print(" Tube:");
//         u8g2.print((int)(totalDispensed * 100 / tubeLifeML));
//         u8g2.print("%");
//       }
//     }
//   u8g2.sendBuffer();
// }

// ----- 设置流量界面 -----
// void drawSetFlow() {
//   u8g2.clearBuffer();
//     u8g2.setFont(u8g2_font_8x13_tr);
//     u8g2.setCursor(0, 13);
//     u8g2.print("Set Flow mL/min");

//     // 大字体显示输入内容, 闪烁下划线模拟光标
//     u8g2.setFont(u8g2_font_10x20_tr);
//     u8g2.setCursor(24, 37);
//     if (inputLen > 0) {
//       u8g2.print(inputBuf);
//       u8g2.setFont(u8g2_font_6x10_tr);
//       u8g2.print("_");             // 光标
//     } else {
//       u8g2.print("---");           // 空输入占位
//     }

//     u8g2.setFont(u8g2_font_6x10_tr);
//     u8g2.setCursor(0, 62);
//     u8g2.print("0-9:In #:Dot *:OK D:Back");
//   u8g2.sendBuffer();
// }

// ----- 设置VOL界面 -----
// void drawSetVol() {
//   u8g2.clearBuffer();
//     u8g2.setFont(u8g2_font_8x13_tr);
//     u8g2.setCursor(0, 13);
//     u8g2.print("Set Volume mL");

//     u8g2.setFont(u8g2_font_10x20_tr);
//     u8g2.setCursor(24, 37);
//     if (inputLen > 0) {
//       u8g2.print(inputBuf);
//       u8g2.setFont(u8g2_font_6x10_tr);
//       u8g2.print("_");
//     } else {
//       u8g2.print("---");
//     }

//     u8g2.setFont(u8g2_font_6x10_tr);
//     u8g2.setCursor(0, 62);
//     u8g2.print("0-9:In #:Dot *:OK D:Back");
//   u8g2.sendBuffer();
// }

// ----- 设置单次JET量界面 -----
// void drawSetJetVol() {
//   u8g2.clearBuffer();
//     u8g2.setFont(u8g2_font_8x13_tr);
//     u8g2.setCursor(0, 13);
//     u8g2.print("Shot Volume mL");

//     u8g2.setFont(u8g2_font_10x20_tr);
//     u8g2.setCursor(24, 37);
//     if (inputLen > 0) {
//       u8g2.print(inputBuf);
//       u8g2.setFont(u8g2_font_6x10_tr);
//       u8g2.print("_");
//     } else {
//       u8g2.print("---");
//     }

//     u8g2.setFont(u8g2_font_6x10_tr);
//     u8g2.setCursor(0, 62);
//     u8g2.print("0-9:In #:Dot *:OK D:Back");
//   u8g2.sendBuffer();
// }

// ----- 设置JET间隔界面 -----
// void drawSetJetInterval() {
//   u8g2.clearBuffer();
//     u8g2.setFont(u8g2_font_8x13_tr);
//     u8g2.setCursor(0, 13);
//     u8g2.print("Interval (sec)");

//     u8g2.setFont(u8g2_font_10x20_tr);
//     u8g2.setCursor(24, 37);
//     if (inputLen > 0) {
//       u8g2.print(inputBuf);
//       u8g2.setFont(u8g2_font_6x10_tr);
//       u8g2.print("_");
//     } else {
//       u8g2.print("---");
//     }

//     u8g2.setFont(u8g2_font_6x10_tr);
//     u8g2.setCursor(0, 62);
//     u8g2.print("0-9:In *:OK D:Back");
//   u8g2.sendBuffer();
// }

// ----- 设置JET压力界面 -----
// void drawSetJetPressure() {
//   u8g2.clearBuffer();
//     u8g2.setFont(u8g2_font_8x13_tr);
//     u8g2.setCursor(0, 13);
//     u8g2.print("Pressure (1-10)");

//     u8g2.setFont(u8g2_font_6x10_tr);
//     u8g2.setCursor(0, 24);
//     u8g2.print("1=Soft 5=Std 10=Max");

//     u8g2.setFont(u8g2_font_10x20_tr);
//     u8g2.setCursor(24, 50);
//     if (inputLen > 0) {
//       u8g2.print(inputBuf);
//       u8g2.setFont(u8g2_font_6x10_tr);
//       u8g2.print("_");
//     } else {
//       u8g2.print("---");
//     }

//     u8g2.setFont(u8g2_font_6x10_tr);
//     u8g2.setCursor(0, 62);
//     u8g2.print("0-9:In *:OK D:Back");
//   u8g2.sendBuffer();
// }

// ----- 设置JET流量界面 -----
// void drawSetJetFlow() {
//   u8g2.clearBuffer();
//     u8g2.setFont(u8g2_font_8x13_tr);
//     u8g2.setCursor(0, 13);
//     u8g2.print("Jet Flow mL/min");

//     u8g2.setFont(u8g2_font_6x10_tr);
//     u8g2.setCursor(0, 24);
//     u8g2.print("Tip2-4mm:W>200 T20-100");

//     u8g2.setFont(u8g2_font_10x20_tr);
//     u8g2.setCursor(24, 50);
//     if (inputLen > 0) {
//       u8g2.print(inputBuf);
//       u8g2.setFont(u8g2_font_6x10_tr);
//       u8g2.print("_");
//     } else {
//       u8g2.print("---");
//     }

//     u8g2.setFont(u8g2_font_6x10_tr);
//     u8g2.setCursor(0, 62);
//     u8g2.print("0-9:In #:Dot *:OK D:Back");
//   u8g2.sendBuffer();
// }

// ----- Jet Settings二选一 (# 键进入) -----
// void drawJetOptions() {
//   u8g2.clearBuffer();
//     u8g2.setFont(u8g2_font_8x13_tr);
//     u8g2.setCursor(0, 13);
//     u8g2.print("Jet Settings");

//     u8g2.setFont(u8g2_font_6x10_tr);
//     u8g2.setCursor(0, 24);
//     u8g2.print("A: Jet Calib");

//     u8g2.setCursor(0, 37);
//     u8g2.print("B: Jet Pres (");
//     u8g2.print((int)jetPressure);
//     u8g2.print(")");

//     u8g2.setCursor(0, 50);
//     u8g2.print("#: Jet Flow (");
//     u8g2.print((int)jetFlowRate);
//     u8g2.print("mL/m)");

//     u8g2.setFont(u8g2_font_5x7_tr);
//     u8g2.setCursor(0, 62);
//     u8g2.print("A:Cal B:Pres #:Flow D:Back");
//   u8g2.sendBuffer();
// }

// ----- 方案预设加载/保存界面 -----
// void drawPresetLoad() {
//   int slot = presetSlot;
//   bool valid = isPresetValid(slot);

//   u8g2.clearBuffer();
//     u8g2.setFont(u8g2_font_8x13_tr);
//     u8g2.setCursor(0, 13);
//     u8g2.print("Slot ");
//     u8g2.print(slot + 1);
//     u8g2.print(valid ? " [已存]" : " (空)");

//     if (!valid) {
//       u8g2.setFont(u8g2_font_6x10_tr);
//       u8g2.setCursor(0, 37);
//       u8g2.print("Empty, press # to save");
//     } else {
//       // 读出参数显示
//       int base = PRESET_BASE + slot * PRESET_SIZE;
//       uint8_t mode = EEPROM.read(base);
//       uint8_t liquid = EEPROM.read(base + 1);
//       float fr, tv, tt, jv, ji, jfr, jpr;
//       EEPROM.get(base + 2,  fr);
//       EEPROM.get(base + 6,  tv);
//       EEPROM.get(base + 10, tt);
//       EEPROM.get(base + 14, jv);
//       EEPROM.get(base + 18, ji);
//       EEPROM.get(base + 22, jfr);
//       EEPROM.get(base + 26, jpr);

//       u8g2.setFont(u8g2_font_6x10_tr);
//       u8g2.setCursor(0, 24);
//       u8g2.print("Mode: ");
//       u8g2.print(mode == 0 ? "体积" : mode == 1 ? "定时" : "喷射");

//       u8g2.setCursor(0, 37);
//       u8g2.print("Liq: ");
//       u8g2.print(liquid < NUM_LIQUIDS ? liquidNames[liquid] : "?");

//       if (mode == 2) { // JET模式特有参数
//         u8g2.setCursor(0, 37);
//         u8g2.print("Shot:"); u8g2.print(jv, 1); u8g2.print("mL");
//         u8g2.print("  间隔:"); u8g2.print((int)ji); u8g2.print("s");
//         u8g2.setCursor(0, 50);
//         u8g2.print("Flow:"); u8g2.print((int)jfr);
//         u8g2.print(" Pres:"); u8g2.print((int)jpr); 
//       } else {
//         u8g2.setCursor(0, 37);
//         u8g2.print("Flow:"); u8g2.print(fr, 1); u8g2.print("mL/min");
//         u8g2.setCursor(0, 50);
//         u8g2.print("体积:"); u8g2.print(tv, 1); u8g2.print("mL");
//         if (mode == 1) {
//           u8g2.print(" 时间:"); u8g2.print((int)tt); u8g2.print("s");
//         }
//       }
//     }

//     u8g2.setFont(u8g2_font_5x7_tr);
//     u8g2.setCursor(0, 62);
//     u8g2.print("*:Load #:Save 1-4:Slot D:Back");
//   u8g2.sendBuffer();
// }

// ----- 独立液体选择 (D 键切换模式后弹出) -----
// void drawSelectLiquid() {
//   u8g2.clearBuffer();
//     u8g2.setFont(u8g2_font_8x13_tr);
//     u8g2.setCursor(0, 13);
//     u8g2.print("Select Liquid");

//     u8g2.setFont(u8g2_font_6x10_tr);
//     for (int i = 0; i < NUM_LIQUIDS; i++) {
//       u8g2.setCursor(10, 28 + i * 10);
//       u8g2.print(i == currentLiquid ? ">" : " ");
//       u8g2.print((char)('A' + i));
//       u8g2.print(": ");
//       u8g2.print(liquidNames[i]);
//     }

//     u8g2.setFont(u8g2_font_6x10_tr);
//     u8g2.setCursor(0, 62);
//     u8g2.print("A/B/C/D:Pick *:Skip");
//   u8g2.sendBuffer();
// }

// ----- 设置时间界面 (带实时格式化预览) -----
// void drawSetTime() {
//   u8g2.clearBuffer();
//     u8g2.setFont(u8g2_font_8x13_tr);
//     u8g2.setCursor(0, 13);
//     u8g2.print("Set Time (sec)");

//     u8g2.setFont(u8g2_font_10x20_tr);
//     u8g2.setCursor(24, 37);
//     if (inputLen > 0) {
//       u8g2.print(inputBuf);
//       u8g2.setFont(u8g2_font_6x10_tr);
//       u8g2.print("_");
//     } else {
//       u8g2.print("---");
//     }

//     // 实时预览: 输入 90 -> 显示 "= 1m30s"
//     if (inputLen > 0) {
//       u8g2.setFont(u8g2_font_6x10_tr);
//       int t = (int)inputToFloat();
//       u8g2.setCursor(50, 50);
//       u8g2.print("= ");
//       u8g2.print(t / 60);
//       u8g2.print("m");
//       u8g2.print(t % 60);
//       u8g2.print("s");
//     }

//     u8g2.setFont(u8g2_font_6x10_tr);
//     u8g2.setCursor(0, 62);
//     u8g2.print("0-9:In #:Dot *:OK D:Back");
//   u8g2.sendBuffer();
// }

// ----- 校准 1/5: 选择要校准的液体 -----
// void drawCalibSelectLiquid() {
//   u8g2.clearBuffer();
//     u8g2.setFont(u8g2_font_8x13_tr);
//     u8g2.setCursor(0, 13);
//     u8g2.print("Cal 1/5 - Pick Liq");

//     u8g2.setFont(u8g2_font_6x10_tr);
//     for (int i = 0; i < NUM_LIQUIDS; i++) {
//       u8g2.setCursor(0, 28 + i * 10);
//       // 当前选中加 ">" 标记
//       u8g2.print(i == currentLiquid ? ">" : " ");
//       u8g2.print((char)('A' + i));          // A/B/C/D
//       u8g2.print(":");
//       u8g2.print(liquidNames[i]);
//       // 显示每个液体的当前校准值
//       u8g2.print(" (");
//       u8g2.print((int)liquidSPM[i]);
//       u8g2.print(")");
//     }

//     u8g2.setFont(u8g2_font_6x10_tr);
//     u8g2.setCursor(0, 62);
//     u8g2.print("A/B/C/D:Pick *:Keep #:Exit");
//   u8g2.sendBuffer();
// }

// ----- Cal 2/5: 设定校准用的目标VOL -----
// void drawCalibSetVol() {
//   u8g2.clearBuffer();
//     u8g2.setFont(u8g2_font_8x13_tr);
//     u8g2.setCursor(0, 13);
//     u8g2.print("Cal 2/5");

//     u8g2.setFont(u8g2_font_6x10_tr);
//     u8g2.setCursor(0, 24);
//     u8g2.print("Calibrate with ? mL");

//     // 默认显示 calibTargetVol (10.0), 输入后覆盖
//     u8g2.setFont(u8g2_font_10x20_tr);
//     u8g2.setCursor(24, 50);
//     if (inputLen > 0) {
//       u8g2.print(inputBuf);
//       u8g2.setFont(u8g2_font_6x10_tr);
//       u8g2.print("_");
//     } else {
//       u8g2.print(calibTargetVol, 1);
//     }

//     u8g2.setFont(u8g2_font_6x10_tr);
//     u8g2.setCursor(0, 62);
//     u8g2.print("0-9In #. *OK D:Back #:Adv");
//   u8g2.sendBuffer();
// }

// ----- 校准 2/4: 运行泵 (放量筒 -> 泵出 -> 观察进度) -----
// void drawCalibRun() {
//   u8g2.clearBuffer();
//     u8g2.setFont(u8g2_font_8x13_tr);
//     u8g2.setCursor(0, 13);
//     u8g2.print("Cal 3/5");

//     // 显示目标VOL和当前流量
//     u8g2.setFont(u8g2_font_6x10_tr);
//     u8g2.setCursor(0, 24);
//     u8g2.print("Tgt:");
//     u8g2.print(calibTargetVol, 1);
//     u8g2.print("mL  "); u8g2.print("Flow:");
//     u8g2.print(flowRate, 1);
//     u8g2.print("mL/min");

//     if (!calibRunning) {
//       // 还没开始 - 提示放量筒
//       u8g2.setFont(u8g2_font_8x13_tr);
//       u8g2.setCursor(0, 50);
//       u8g2.print("Ready, press C");
//     } else {
//       // 运行中 - 显示实时出液量和进度条
//       u8g2.setCursor(0, 37);
//       u8g2.print("Out:");
//       u8g2.print(dispensedVolume, 1);
//       u8g2.print(" mL");
//       if (calibTargetVol > 0) {
//         int barW = map(constrain(dispensedVolume * 100 / calibTargetVol, 0, 100), 0, 100, 0, 126);
//         u8g2.drawBox(0, 57, barW, 4);
//       }
//     }

//     u8g2.setFont(u8g2_font_6x10_tr);
//     u8g2.setCursor(0, 62);
//     if (!calibRunning) {
//       u8g2.print("C:Start D:Back");
//     } else if (pumpState == DONE) {
//       u8g2.print("Done! *:Next");      // 泵跑完了, 提示进入下一步
//     } else {
//       u8g2.print("C:Stop (running)");
//     }
//   u8g2.sendBuffer();
// }

// ----- 校准 3/4: 用量筒读数, 输入实测值 -----
// void drawCalibMeasure() {
//   u8g2.clearBuffer();
//     u8g2.setFont(u8g2_font_8x13_tr);
//     u8g2.setCursor(0, 13);
//     u8g2.print("Cal 4/5");

//     u8g2.setFont(u8g2_font_6x10_tr);
//     u8g2.setCursor(0, 24);
//     u8g2.print("Measured (mL):");

//     u8g2.setFont(u8g2_font_10x20_tr);
//     u8g2.setCursor(24, 50);
//     if (inputLen > 0) {
//       u8g2.print(inputBuf);
//       u8g2.setFont(u8g2_font_6x10_tr);
//       u8g2.print("_");
//     } else {
//       u8g2.print("---");
//     }

//     u8g2.setFont(u8g2_font_6x10_tr);
//     u8g2.setCursor(0, 62);
//     u8g2.print("0-9In #. *OK D:Back");
//   u8g2.sendBuffer();
// }

// ----- 校准 4/4: 显示计算结果 -----
// void drawCalibResult() {
//   u8g2.clearBuffer();
//     u8g2.setFont(u8g2_font_8x13_tr);
//     u8g2.setCursor(0, 13);
//     u8g2.print("Cal 5/5 - Result");

//     u8g2.setFont(u8g2_font_6x10_tr);
//     // 旧值
//     u8g2.setCursor(0, 24);
//     u8g2.print("Old:");
//     u8g2.print((int)stepsPerMl);
//     u8g2.print(" step/mL");

//     // 新值 (自动算出)
//     u8g2.setCursor(0, 37);
//     u8g2.print("New:");
//     u8g2.print((int)calibNewSPM);
//     u8g2.print(" step/mL");

//     // 底层数据: 实际步数 / 实测VOL
//     u8g2.setCursor(0, 50);
//     u8g2.print("Steps:");
//     u8g2.print(calibStepsRun);
//     u8g2.print("  量筒:");
//     u8g2.print(calibActualVol, 1);
//     u8g2.print("mL");

//     u8g2.setFont(u8g2_font_6x10_tr);
//     u8g2.setCursor(0, 62);
//     u8g2.print("*:Save D:Discard");
//   u8g2.sendBuffer();
// }

// ----- Cal 5/5: Advanced (回吸量 / 管路寿命) -----
// void drawCalibSettings() {
//   u8g2.clearBuffer();
//     u8g2.setFont(u8g2_font_8x13_tr);
//     u8g2.setCursor(0, 13);
//     u8g2.print("Advanced");

//     u8g2.setFont(u8g2_font_6x10_tr);

//     // 当前液体 (只读显示)
//     u8g2.setCursor(0, 24);
//     u8g2.print("液体:");
//     u8g2.print(liquidNames[currentLiquid]);
//     u8g2.print(" (");
//     u8g2.print((int)liquidSPM[currentLiquid]);
//     u8g2.print("spm)");

//     // 回吸量 (2 位小数)
//     u8g2.setCursor(0, 37);
//     u8g2.print("Anti-drip:");
//     u8g2.print(antiDripVol, 2);
//     u8g2.print(" mL (0=off)");

//     // 管路寿命 (≥1000mL 自动切换为 L 显示)
//     u8g2.setCursor(0, 50);
//     u8g2.print("Tube:");
//     if (tubeLifeML >= 1000) {
//       u8g2.print(tubeLifeML / 1000, 1);
//       u8g2.print("L");
//     } else {
//       u8g2.print((int)tubeLifeML);
//       u8g2.print("mL");
//     }
//     u8g2.print(" (0=disabled)");

//     // 编辑状态: 显示输入提示和当前输入
//     if (settingEdit == SET_LIQUID) {
//       // 液体选择界面覆盖在设置上
//       u8g2.setCursor(0, 50);
//       u8g2.print("A:W B:T C:O D:C");
//     } else if (settingEdit != SET_NONE) {
//       u8g2.setCursor(0, 50);
//       u8g2.print(settingEdit == SET_ANTI_DRIP ? "> 输入回吸量:" : "> 输入管寿命:");
//       u8g2.setCursor(72, 50);
//       if (inputLen > 0) u8g2.print(inputBuf);
//       else u8g2.print("---");
//       u8g2.print("_");
//     }

//     u8g2.setFont(u8g2_font_5x7_tr);
//     u8g2.setCursor(0, 62);
//     if (settingEdit != SET_NONE) {
//       u8g2.print("0-9 #. *OK D:Cancel");
//     } else {
//       u8g2.print("A:Drip B:Tube C:Liq *:Back");
//     }
//   u8g2.sendBuffer();
// }

// ----- 校准: 根据 calibStep 分发到具体的绘制函数 -----
// void drawCalibrate() {
//   switch (calibStep) {
//     case CALIB_SELECT_LIQUID: drawCalibSelectLiquid(); break;
//     case CALIB_SET_VOL:  drawCalibSetVol();   break;
//     case CALIB_RUN:      drawCalibRun();      break;
//     case CALIB_MEASURE:  drawCalibMeasure();  break;
//     case CALIB_RESULT:   drawCalibResult();   break;
//     case CALIB_SETTINGS: drawCalibSettings(); break;
//     default: break;
//   }
// }

// ----- PRIME/PURGE界面 -----
// void drawPrime() {
//   u8g2.clearBuffer();
//     u8g2.setFont(u8g2_font_8x13_tr);
//     u8g2.setCursor(0, 13);
//     u8g2.print("== PRIME ==");

//     const char* s = (pumpState == RUNNING) ? "Run" : "Idle";
//     u8g2.setCursor(78, 13);
//     u8g2.print(s);

//     // 固定最大流量
//     u8g2.setCursor(0, 24);
//     u8g2.print("Flow:2000 mL/min");

//     // 已灌VOL (从 0 累加)
//     u8g2.setCursor(0, 37);
//     u8g2.print("Out:");
//     u8g2.print(dispensedVolume, 1);
//     u8g2.print(" mL");

//     // 管路寿命 (方便换管时参考)
//     if (tubeLifeML > 0) {
//       u8g2.setFont(u8g2_font_6x10_tr);
//       u8g2.setCursor(0, 50);
//       int pct = (int)(totalDispensed * 100 / tubeLifeML);
//       u8g2.print("Tube:");
//       u8g2.print(constrain(pct, 0, 100));
//       u8g2.print("% (");
//       u8g2.print((int)totalDispensed);
//       u8g2.print("mL)");
//     }

//     u8g2.setFont(u8g2_font_5x7_tr);
//     u8g2.setCursor(0, 62);
//     u8g2.print("C:Run/Stop D:Exit");
//   u8g2.sendBuffer();
// }

// ----- 屏幕总调度: 限 150ms 刷新率, 根据 currentMenu 分发 -----
// void updateDisplay() {
//   // 根据当前菜单/状态调用对应的绘制函数
//   switch (currentMenu) {
//     case SET_FLOW:          drawSetFlow();          break;
//     case SET_VOL:           drawSetVol();           break;
//     case SET_TIME:          drawSetTime();          break;
//     case SET_JET_VOL:       drawSetJetVol();        break;
//     case SET_JET_INTERVAL:  drawSetJetInterval();   break;
//     case SET_JET_FLOW:      drawSetJetFlow();       break;
//     case SET_JET_PRESSURE:  drawSetJetPressure();   break;
//     case JET_OPTIONS:       drawJetOptions();       break;
//     case CALIBRATE:         drawCalibrate();        break;
//     case PRESET_LOAD:       drawPresetLoad();       break;
//     case SELECT_LIQUID:     drawSelectLiquid();     break;
//     case PRIME:             drawPrime();            break;
//     default:                drawMain();             break;
//   }
// }

// ============================================================================
//                         数字输入处理
// ============================================================================
// 在所有输入界面 (设置流量/VOL/时间/校准) 共用
// 返回 true 表示按键已被消费, 调用者 (handleXxx) 不需再处理
//
// 键位约定 (输入模式):
//   0-9 : 数字
//   C   : 退格 (仅当已有输入时)
//   #   : 小数点 (仅当还没有小数点时)
//         如果已有小数点, 返回 false 让上层处理 (如确认)

// bool handleNumericInput(char key) {
//   // 数字键
//   if (key >= '0' && key <= '9') {
//     inputAppend(key);
//     beepInput();
//     return true;
//   }
//   // C 键 = 退格 (仅在输入模式下, 有内容可退时)
//   if (key == 'C' && inputLen > 0) {
//     inputBackspace();
//     beepInput();
//     return true;
//   }
//   // # 键 = 小数点
//   if (key == '#') {
//     // 检查当前输入是否已有小数点
//     bool hasDot = false;
//     for (int i = 0; i < inputLen; i++) {
//       if (inputBuf[i] == '.') { hasDot = true; break; }
//     }
//     if (!hasDot) {
//       inputAppend('.');          // 插入小数点
//       beepInput();
//       return true;               // 已消费
//     }
//     // 已经有小数点了 -> 不消费, 让调用者把 # 当作"确认"
//     return false;
//   }
//   return false;                  // 未识别的按键, 交给调用者
// }

// ============================================================================
//                          键盘分发 (按键 -> 功能)
// ============================================================================

// ----- 主菜单按键 (currentMenu == MAIN) -----
// void handleMain(char key) {
//   switch (key) {
//     case '1': case '2': case '3': case '4':
//       presetSlot = key - '1';
//       currentMenu = PRESET_LOAD;
//       break;

//     case 'A':
//       // VOL->设流量, TIMED->设VOL, JET->设单次量
//       if (pumpMode == MODE_TIME)      currentMenu = SET_VOL;
//       else if (pumpMode == MODE_JET)  currentMenu = SET_JET_VOL;
//       else                            currentMenu = SET_FLOW;
//       inputClear();
//       break;

//     case 'B':
//       // VOL->设VOL, TIMED->设时间, JET->设间隔
//       if (pumpMode == MODE_TIME)      currentMenu = SET_TIME;
//       else if (pumpMode == MODE_JET)  currentMenu = SET_JET_INTERVAL;
//       else                            currentMenu = SET_VOL;
//       inputClear();
//       break;

//     case '0':
//       // PRIME/PURGE: 清零VOL计数, 切换到预灌界面
//       dispensedVolume = 0;
//       stepper.setCurrentPosition(0);
//       currentMenu = PRIME;
//       break;

//     case 'C':
//       // JETMode: 启停循环; 其他Mode: 启/停/暂停/恢复
//       if (pumpMode == MODE_JET) {
//         if (pumpState == RUNNING)      stopJetCycle();
//         else if (pumpState == STATE_IDLE)    startJetCycle();
//       } else {
//         switch (pumpState) {
//           case STATE_IDLE:      startPump();  break;   // 启动
//           case RUNNING:   pausePump();  break;   // 暂停
//           case PAUSED:    resumePump(); break;   // 恢复
//           case DONE:      resetPump();  beepCancel(); break;  // 复位
//           case ANTI_DRIP: break;                 // SUCKING不可打断
//         }
//       }
//       break;

//     case 'D':
//       // 三态循环: VOL->TIMED->JET->VOL, 同时复位泵
//       if (pumpMode == MODE_VOLUME)      pumpMode = MODE_TIME;
//       else if (pumpMode == MODE_TIME)   pumpMode = MODE_JET;
//       else                              pumpMode = MODE_VOLUME;
//       beepConfirm();
//       markDirty();               // 模式变了, 标记需要写 EEPROM
//       resetPump();
//       jetCount = 0;              // 切出JET模式清零计数
//       currentMenu = SELECT_LIQUID;  // 弹出液体选择
//       break;

//     case '*':
//       // 复位泵 (清零所有计数)
//       resetPump();
//       jetCount = 0;
//       beepCancel();
//       break;

//     case '#':
//       // JET模式->二选一(校准or设流量), 其他->校准向导
//       if (pumpMode == MODE_JET) {
//         currentMenu = JET_OPTIONS;
//       } else {
//         beepConfirm();
//         calibEnter();
//       }
//       break;
//   }
// }

// ----- 设置流量界面按键 -----
// void handleSetFlow(char key) {
//   if (handleNumericInput(key)) return;   // 数字/小数点/退格 -> 已处理

//   if (key == '*' && inputLen > 0) {
//     // * 确认: 保存流量值
//     flowRate = constrain(inputToFloat(), 0.1, 2000.0);
//     beepConfirm();
//     markDirty();                // 参数改变, 标记 EEPROM 脏
//     inputClear();
//     currentMenu = MAIN;
//   }
//   if (key == 'D') {
//     // D 返回: 放弃修改
//     beepCancel();
//     currentMenu = MAIN;
//     inputClear();
//   }
// }

// ----- 设置VOL界面按键 -----
// void handleSetVol(char key) {
//   if (handleNumericInput(key)) return;

//   if (key == '*' && inputLen > 0) {
//     targetVolume = constrain(inputToFloat(), 0.1, 99999);
//     beepConfirm();
//     markDirty();
//     inputClear();
//     currentMenu = MAIN;
//   }
//   if (key == 'D') {
//     beepCancel();
//     currentMenu = MAIN;
//     inputClear();
//   }
// }

// ----- 设置单次JET量按键 -----
// void handleSetJetVol(char key) {
//   if (handleNumericInput(key)) return;
//   if (key == '*' && inputLen > 0) {
//     jetVolume = constrain(inputToFloat(), 0.1, 10.0);
//     beepConfirm();
//     markDirty();
//     inputClear();
//     currentMenu = MAIN;
//   }
//   if (key == 'D') {
//     beepCancel();
//     currentMenu = MAIN;
//     inputClear();
//   }
// }

// ----- 设置JET间隔按键 -----
// void handleSetJetInterval(char key) {
//   // 间隔是整数秒, 小数点在这里无意义, 所以不用 handleNumericInput
//   if (key >= '0' && key <= '9') {
//     inputAppend(key);
//     beepInput();
//     return;
//   }
//   if (key == 'C' && inputLen > 0) {
//     inputBackspace();
//     beepInput();
//     return;
//   }
//   if (key == '*' && inputLen > 0) {
//     jetInterval = constrain(inputToFloat(), 1, 60);
//     beepConfirm();
//     markDirty();
//     inputClear();
//     currentMenu = MAIN;
//   }
//   if (key == 'D') {
//     beepCancel();
//     currentMenu = MAIN;
//     inputClear();
//   }
// }

// ----- 设置JET压力按键 -----
// void handleSetJetPressure(char key) {
//   // 压力是整数 1-10, 不需要小数点
//   if (key >= '0' && key <= '9') {
//     inputAppend(key);
//     beepInput();
//     return;
//   }
//   if (key == 'C' && inputLen > 0) {
//     inputBackspace();
//     beepInput();
//     return;
//   }
//   if (key == '*' && inputLen > 0) {
//     jetPressure = constrain(inputToFloat(), 1, 10);
//     beepConfirm();
//     markDirty();
//     inputClear();
//     currentMenu = MAIN;
//   }
//   if (key == 'D') {
//     beepCancel();
//     currentMenu = MAIN;
//     inputClear();
//   }
// }

// ----- 设置JET流量按键 -----
// void handleSetJetFlow(char key) {
//   if (handleNumericInput(key)) return;
//   if (key == '*' && inputLen > 0) {
//     jetFlowRate = constrain(inputToFloat(), 10, 2000.0);
//     beepConfirm();
//     markDirty();
//     inputClear();
//     currentMenu = MAIN;
//   }
//   if (key == 'D') {
//     beepCancel();
//     currentMenu = MAIN;
//     inputClear();
//   }
// }

// ----- Jet Settings二选一按键 -----
// void handleJetOptions(char key) {
//   switch (key) {
//     case 'A':
//       beepConfirm();
//       calibEnter();                   // 进入校准向导, pumpMode 保持 MODE_JET
//       break;
//     case 'B':
//       currentMenu = SET_JET_PRESSURE; // 设JET压力
//       inputClear();
//       break;
//     case '#':
//       currentMenu = SET_JET_FLOW;     // 设JET流量
//       inputClear();
//       break;
//     case 'D':
//       beepCancel();
//       currentMenu = MAIN;
//       break;
//   }
// }

// ----- 方案预设加载/保存按键 -----
// void handlePresetLoad(char key) {
//   switch (key) {
//     case '1': case '2': case '3': case '4':
//       // 在方案界面按数字 = 保存到该槽位
//       savePreset(key - '1');
//       beepConfirm();
//       currentMenu = MAIN;
//       break;
//     case '*':
//       if (isPresetValid(presetSlot)) {
//         loadPreset(presetSlot);
//         beepConfirm();
//         currentMenu = MAIN;
//       }
//       break;
//     case '#':
//       savePreset(presetSlot);
//       beepConfirm();
//       currentMenu = MAIN;
//       break;
//     case 'D':
//       beepCancel();
//       currentMenu = MAIN;
//       break;
//   }
// }

// ----- 独立液体选择按键 (D 键切换模式后弹出) -----
// void handleSelectLiquid(char key) {
//   if (key >= 'A' && key <= 'D') {
//     int idx = key - 'A';
//     if (idx < NUM_LIQUIDS) {
//       selectLiquid(idx);
//       beepConfirm();
//       currentMenu = MAIN;
//     }
//   }
//   if (key == '*') {
//     beepConfirm();                 // 保持当前液体
//     currentMenu = MAIN;
//   }
// }

// ----- 设置时间界面按键 -----
// void handleSetTime(char key) {
//   if (handleNumericInput(key)) return;

//   if (key == '*' && inputLen > 0) {
//     targetTime = constrain(inputToFloat(), 1, 86400);  // 1 秒 ~ 24 小时
//     beepConfirm();
//     markDirty();
//     inputClear();
//     currentMenu = MAIN;
//   }
//   if (key == 'D') {
//     beepCancel();
//     currentMenu = MAIN;
//     inputClear();
//   }
// }

// ----- 校准向导按键 (含 5 个子步骤) -----
// void handleCalibrate(char key) {
//   switch (calibStep) {

//     // ---- 1/5: Select Liquid ----
//     case CALIB_SELECT_LIQUID:
//       if (key >= 'A' && key <= 'D') {
//         int idx = key - 'A';                     // A=0, B=1, C=2, D=3
//         if (idx < NUM_LIQUIDS) {
//           selectLiquid(idx);
//           beepConfirm();
//           calibStep = CALIB_SET_VOL;             // 进入第 2 步
//           inputClear();
//         }
//       }
//       if (key == '*') {
//         beepConfirm();                           // 保持当前液体直接进入
//         calibStep = CALIB_SET_VOL;
//         inputClear();
//       }
//       if (key == '#') {
//         beepCancel();                            // 退出校准
//         currentMenu = MAIN;
//         calibStep = CALIB_IDLE;
//       }
//       break;

//     // ---- 2/5: 设定校准VOL ----
//     case CALIB_SET_VOL:
//       // 空输入时按 # = 跳转到Advanced (不输入任何数字直接按 #)
//       if (key == '#' && inputLen == 0) {
//         calibStep = CALIB_SETTINGS;
//         settingEdit = SET_NONE;
//         inputClear();
//         break;
//       }
//       if (handleNumericInput(key)) return;
//       if (key == '*' && inputLen > 0) {
//         calibTargetVol = constrain(inputToFloat(), 0.5, 500.0);
//         beepConfirm();
//         inputClear();
//         calibStep = CALIB_RUN;     // 进入第 2 步
//       }
//       if (key == 'D') {
//         beepCancel();
//         calibStep = CALIB_SELECT_LIQUID;  // 回第 1 步重选液体
//         inputClear();
//       }
//       break;

//     // ---- 3/5: 运行泵 ----
//     case CALIB_RUN:
//       if (key == 'C') {
//         if (!calibRunning) {
//           calibStartRun();         // 启动校准泵
//         } else {
//           calibStopRun();          // 手动停止
//           beepCancel();
//         }
//       }
//       // 泵自己跑完 (pumpState==DONE) 后按 * 进入下一步
//       if (key == '*' && pumpState == DONE) {
//         calibFinishRun();          // 记录实际步数
//         beepConfirm();
//         calibStep = CALIB_MEASURE;
//         inputClear();
//       }
//       // D: 停止并返回上一步 (即使正在运行也允许)
//       if (key == 'D') {
//         if (calibRunning) { calibStopRun(); }  // 先停泵
//         beepCancel();
//         calibStep = CALIB_SET_VOL;
//         inputClear();
//       }
//       // #: 完全退出校准 (即使正在运行也允许)
//       if (key == '#') {
//         if (calibRunning) { calibStopRun(); }
//         resetPump();
//         beepCancel();
//         currentMenu = MAIN;
//         calibStep = CALIB_IDLE;
//       }
//       break;

//     // ---- 3/4: 输入实测VOL ----
//     case CALIB_MEASURE:
//       if (handleNumericInput(key)) return;
//       if (key == '*' && inputLen > 0) {
//         calibActualVol = constrain(inputToFloat(), 0.1, 99999);
//         calibCalculate();          // 立即计算新的 stepsPerMl
//         beepConfirm();
//         inputClear();
//         calibStep = CALIB_RESULT;  // 显示结果
//       }
//       if (key == 'D') {
//         beepCancel();
//         calibStep = CALIB_RUN;     // 回第 2 步 (重新跑)
//         inputClear();
//       }
//       break;

//     // ---- 4/4: 确认结果 ----
//     case CALIB_RESULT:
//       if (key == '*') {
//         calibSave();               // 保存新 stepsPerMl 到 EEPROM
//         beepConfirm();
//         calibStep = CALIB_SETTINGS; // 保存后自动进入Advanced
//         settingEdit = SET_NONE;
//         inputClear();
//       }
//       if (key == 'D') {
//         beepCancel();              // 放弃新值, 保留旧 stepsPerMl
//         currentMenu = MAIN;
//         calibStep = CALIB_IDLE;
//       }
//       break;

//     // ---- 5/5: Advanced ----
//     case CALIB_SETTINGS:
//       if (settingEdit != SET_NONE) {
//         // 正在编辑某个数值 (回吸量 或 管寿命)
//         if (handleNumericInput(key)) return;
//         if (key == '*' && inputLen > 0) {
//           float val = constrain(inputToFloat(), 0, 200000);
//           if (settingEdit == SET_ANTI_DRIP) {
//             antiDripVol = constrain(val, 0, 5.0);  // 回吸量 0~5mL
//           } else {
//             tubeLifeML = val;                        // 管寿命 0~200000mL
//           }
//           markDirty();
//           beepConfirm();
//           settingEdit = SET_NONE;  // 退出编辑模式
//           inputClear();
//         }
//         if (key == 'D') {
//           beepCancel();
//           settingEdit = SET_NONE;  // 放弃编辑
//           inputClear();
//         }
//       } else if (settingEdit == SET_LIQUID) {
//         // 液体选择模式 (A/B/C/D 选液体)
//         if (key >= 'A' && key <= 'D') {
//           int idx = key - 'A';
//           if (idx < NUM_LIQUIDS) {
//             selectLiquid(idx);
//             beepConfirm();
//             settingEdit = SET_NONE;
//           }
//         }
//         if (key == '*' || key == 'D') {
//           beepCancel();                  // 取消液体选择
//           settingEdit = SET_NONE;
//         }
//       } else {
//         // 选择要编辑的项目
//         if (key == 'A') {
//           settingEdit = SET_ANTI_DRIP;  // 编辑回吸量
//           inputClear();
//         }
//         if (key == 'B') {
//           settingEdit = SET_TUBE_LIFE;  // 编辑管寿命
//           inputClear();
//         }
//         if (key == 'C') {
//           settingEdit = SET_LIQUID;     // Select Liquid
//         }
//         if (key == '*') {
//           beepConfirm();
//           currentMenu = MAIN;      // 退出Advanced
//           calibStep = CALIB_IDLE;
//         }
//       }
//       break;

//     default: break;
//   }
// }

// ----- PRIME/PURGE界面按键 -----
// void handlePrime(char key) {
//   switch (key) {
//     case 'C':
//       if (pumpState == RUNNING) {
//         // 停止
//         stepper.stop();
//         pumpState = STATE_IDLE;
//       } else {
//         // 启动: 全速 (2000 mL/min) 恒速运转, 无目标位置
//         ensureStepperOn();
//         stepper.setMaxSpeed(flowRateToPPS(2000));
//         stepper.setSpeed(flowRateToPPS(2000));
//         stepper.setCurrentPosition(0);
//         stepper.moveTo(99999999);    // 设一个巨远的目标, 等效于无限跑
//         pumpState = RUNNING;
//         beepStart();
//       }
//       break;

//     case 'D':
//       // 退出预灌, 恢复之前的流量设定
//       stepper.stop();
//       pumpState = STATE_IDLE;
//       beepCancel();
//       currentMenu = MAIN;
//       updateStepperSpeed();          // 重新按原 flowRate 设速度
//       break;
//   }
// }

// ----- 顶层按键路由: 根据 currentMenu 分发 -----
// void handleKey(char key) {
//   switch (currentMenu) {
//     case MAIN:           handleMain(key);           break;
//     case SET_FLOW:       handleSetFlow(key);        break;
//     case SET_VOL:        handleSetVol(key);         break;
//     case SET_TIME:       handleSetTime(key);        break;
//     case CALIBRATE:      handleCalibrate(key);      break;
//     case PRIME:          handlePrime(key);          break;
//     case SET_JET_VOL:    handleSetJetVol(key);      break;
//     case SET_JET_INTERVAL: handleSetJetInterval(key); break;
//     case SET_JET_FLOW:   handleSetJetFlow(key);     break;
//     case SET_JET_PRESSURE: handleSetJetPressure(key); break;
//     case SELECT_LIQUID:  handleSelectLiquid(key);   break;
//     case JET_OPTIONS:    handleJetOptions(key);     break;
//     case PRESET_LOAD:    handlePresetLoad(key);     break;
//   }
// }

// ============================================================================
//                              初始化
// ============================================================================
void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("[SETUP] start");

  // Step 1: EEPROM
  EEPROM.begin(512);
  if (!loadParams()) saveParams();  // 首次使用或数据损坏时写入默认值
  Serial.println("[SETUP] eeprom ok");

  // Step 2: GPIO + Stepper
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  pinMode(ENA_PIN, OUTPUT);
  digitalWrite(ENA_PIN, LOW);
  stepper.setEnablePin(ENA_PIN);
  stepper.enableOutputs();
  stepperEnabled = true;
  lastStepperActivity = millis();
  updateStepperSpeed();
  Serial.println("[SETUP] gpio ok");

  // lastUserActivity = millis(); // keypad disabled

  initSerialCommands();
  Serial.println("[SETUP] serial ok");

  initHardwareUart();
  Serial.println("[SETUP] hw uart ok (GPIO 48=RX, 47=TX)");

  // Step 3: OLED
  // pinMode(I2C_SDA, INPUT_PULLUP); // OLED disabled
  // pinMode(I2C_SCL, INPUT_PULLUP); // OLED disabled
  // u8g2.begin(); // OLED disabled
  // Serial.println("[SETUP] oled ok"); // OLED disabled

  // Step 4: BLE (先于 WiFi, 避免无线冲突)
  initBluetooth();
  Serial.println("[SETUP] ble ok");

  // Step 5: WiFi + Web Server
  initWiFi();
  initWebServer();
  Serial.println("[SETUP] wifi ok");

  Serial.println("[SETUP] done");
}

// ============================================================================
//                              主循环
// ============================================================================
// loop() 的执行顺序:
//   1. 扫描键盘 -> 处理按键 -> 刷新用户活动时间 -> 唤醒屏保
//   2. 回吸状态处理 (ANTI_DRIP)
//   3. 运行状态处理 (正常泵送 / 校准 / 预灌)
//   4. DONE 瞬间响完成音
//   5. 空闲自动关使能
//   6. 闲时 EEPROM 写入
//   7. 屏保检查
//   8. 更新 OLED 显示

void loop() {
  // ---- 第 1 步: 键盘扫描 ----
  // --- Keypad disabled ---
  // char key = keypad.getKey();
  // if (key) {
  //   Serial.print("[KEY] "); Serial.println(key);
  //   handleKey(key);
  //   lastUserActivity = millis();
  //   if (screensaverActive) screensaverActive = false;
  // }

  // ---- 第 1.5 步: 串口命令 ----
  processSerialCommands();
  processHardwareUart();
  handleWebClients();
  handleBluetooth();

  // ---- 第 2 步: 回吸状态 ----
  if (pumpState == ANTI_DRIP) {
    lastStepperActivity = millis();
    if (stepper.distanceToGo() == 0) {
      pumpState = DONE;
      totalDispensed += targetVolume;
      completionCount++;
      if (completionCount >= COMPLETIONS_PER_SAVE) {
        markDirty(); completionCount = 0;
      }
      beepDone();
    }
    stepper.run();
  }

  // ---- 第 3 步: 运行状态 ----
  if (pumpState == RUNNING) {
    lastStepperActivity = millis();
    if (calibRunning) {
      if (stepper.distanceToGo() == 0) {
        dispensedVolume = calibTargetVol;
        calibFinishRun();
      } else {
        dispensedVolume = (float)stepper.currentPosition() / stepsPerMl;
      }
      stepper.run();
    } else if (currentMenu == PRIME) {
      dispensedVolume = (float)stepper.currentPosition() / stepsPerMl;
      stepper.runSpeedToPosition();
    } else if (pumpMode == MODE_JET) {
      if (jetSquirting) {
        if (stepper.distanceToGo() == 0) {
          jetCount++;
          dispensedVolume = jetCount * jetVolume;
          totalDispensed += jetVolume;
          completionCount++;
          if (completionCount >= COMPLETIONS_PER_SAVE) { markDirty(); completionCount = 0; }
          jetSquirting = false;
          jetWaitStart = millis();
        }
        stepper.run();
      } else {
        unsigned long elapsed = millis() - jetWaitStart;
        unsigned long intervalMs = (unsigned long)(jetInterval * 1000);
        if (jetInterval > 15 && stepperEnabled) { stepper.disableOutputs(); stepperEnabled = false; beepDisable(); }
        if (!stepperEnabled && jetInterval > 15 && elapsed >= intervalMs - 2000) { stepper.enableOutputs(); stepperEnabled = true; }
        if (elapsed >= intervalMs) startJetSquirt();
      }
    } else {
      pumpElapsed = (millis() - pumpStartMs) / 1000;
      if (stepper.distanceToGo() == 0) {
        dispensedVolume = targetVolume;
        if (antiDripVol > 0) {
          stepper.setMaxSpeed(flowRateToPPS(flowRate) * 0.3);
          stepper.setSpeed(0);
          stepper.setAcceleration(flowRateToPPS(flowRate));
          stepper.setCurrentPosition(0);
          stepper.moveTo(-(long)(antiDripVol * stepsPerMl));
          pumpState = ANTI_DRIP;
        } else {
          pumpState = DONE;
          totalDispensed += targetVolume;
          completionCount++;
          if (completionCount >= COMPLETIONS_PER_SAVE) { markDirty(); completionCount = 0; }
        }
      } else {
        dispensedVolume = (float)stepper.currentPosition() / stepsPerMl;
        if (pumpMode == MODE_TIME && pumpElapsed >= pumpDuration + 1) {
          stepper.stop();
          pumpState = DONE;
          totalDispensed += targetVolume;
          completionCount++;
          if (completionCount >= COMPLETIONS_PER_SAVE) { markDirty(); completionCount = 0; }
        }
      }
      stepper.run();
    }
  }

  // ---- 第 4 步: DONE 音效 ----
  if (pumpState == DONE && prevPumpState != DONE && prevPumpState != ANTI_DRIP) beepDone();
  prevPumpState = pumpState;

  // ---- 第 5 步: 空闲关使能 ----
  checkIdleDisable();

  // ---- 第 6 步: EEPROM ----
  if (eepromDirty && (pumpState == STATE_IDLE || pumpState == DONE)) saveParams();

  // ---- 第 7 步: 屏保 ----
  // if (!screensaverActive && millis() - lastUserActivity > SCREENSAVER_MS) screensaverActive = true; // OLED disabled

  // ---- 第 8 步: 显示 ----
  // updateDisplay(); // OLED disabled

}
