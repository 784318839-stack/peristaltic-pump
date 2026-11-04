/******************************************************************************
 * minimal_test.ino - ESP32-S3 最简启动测试（2026-10-16 排查开机循环时留下的诊断工具）
 *
 * 三条互相独立的证据路径，任一条成立就说明 setup() 跑过了：
 *   1. GPIO 5 拉高 500 ms 再周期性闪 —— **最可靠的一条**，万用表直流档直接量，
 *      不依赖串口、不依赖 USB 枚举、不依赖任何库
 *   2. Serial 输出 "===== BOOT OK ====="
 *   3. GPIO 48 —— 见下方警告，这条在本板上基本没用
 *
 * ⚠️ 关于路径 2（原文写「Serial (USB CDC) 输出到串口监视器」，是错的）：
 *    `Serial` 指向哪里取决于**编译这个 sketch 时**的 "USB CDC On Boot" 选项，
 *    与生产固件的设置无关（生产固件在 platformio.ini 里钉死了
 *    -DARDUINO_USB_CDC_ON_BOOT=0，那边 Serial = UART0 / GPIO43+44）。
 *    ⇒ 若这里 CDC 选的是 Disabled 而你插的是 USB 口，串口监视器会**一片空白，
 *    但这不代表代码没跑**。这正是排查开机循环时最容易误判的一点：
 *    要么把本 sketch 的 USB CDC On Boot 设为 Enabled，要么接 USB-TTL 到 GPIO43/44。
 *    拿不准就别依赖这条，看路径 1。
 *
 * ⚠️ 关于路径 3（原文写「GPIO 48 如果是普通 LED 会亮」）：
 *    本板（ESP32-S3-DevKitC-1 N16R8）GPIO48 是**单颗 WS2812 可寻址灯珠**，
 *    不是普通 LED —— 生产固件用 Adafruit_NeoPixel 驱动它（见 pump_shared.h 的
 *    LED_PIN 与 led.cpp）。`digitalWrite(48, HIGH)` **不会让它亮**，
 *    只会往数据线发一个恒高电平。这里刻意不引入 NeoPixel 依赖（那就不是
 *    "最简"测试了），所以把 GPIO48 当指示灯用是无效的，别据此判断代码没跑。
 ******************************************************************************/

#define TEST_PIN  5   // 蜂鸣器引脚 —— 万用表直流档量，这是最可靠的一条证据
#define LED_PIN   48  // WS2812 数据线，不是普通 LED；digitalWrite 点不亮（见文件头警告）

void setup() {
  // 1. 先翻 GPIO，即使用户没开串口也能测
  pinMode(TEST_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);

  // 启动标识: GPIO5 拉高 500ms
  digitalWrite(TEST_PIN, HIGH);
  digitalWrite(LED_PIN, HIGH);
  delay(500);
  digitalWrite(TEST_PIN, LOW);
  digitalWrite(LED_PIN, LOW);

  // 2. 初始化串口
  Serial.begin(115200);
  delay(500);
  Serial.println("\n\n===== BOOT OK =====");
  Serial.println("If you see this, setup() executed successfully.");
  Serial.print("Chip: ESP32-S3, PSRAM: ");
  Serial.println(psramFound() ? "FOUND" : "NOT FOUND");
  Serial.print("PSRAM size: ");
  Serial.print(ESP.getPsramSize() / 1024 / 1024);
  Serial.println(" MB");
  Serial.print("Flash size: ");
  Serial.print(ESP.getFlashChipSize() / 1024 / 1024);
  Serial.println(" MB");
}

void loop() {
  // 心跳: GPIO5 和 LED 同步闪
  digitalWrite(TEST_PIN, HIGH);
  digitalWrite(LED_PIN, HIGH);
  Serial.println("HEARTBEAT");
  delay(100);
  digitalWrite(TEST_PIN, LOW);
  digitalWrite(LED_PIN, LOW);
  delay(900);
}
