/******************************************************************************
 * minimal_test.ino - ESP32-S3 最简启动测试
 *
 * 多路径验证代码是否执行:
 *   1. Serial (USB CDC) 输出到串口监视器
 *   2. GPIO 5 产生 PWM 方波 (万用表交流档可测)
 *   3. GPIO 48 如果是普通 LED 会亮
 ******************************************************************************/

#define TEST_PIN  5   // 蜂鸣器引脚 - 用万用表测
#define LED_PIN   48  // 板载 LED

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
