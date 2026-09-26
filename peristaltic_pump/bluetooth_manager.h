/******************************************************************************
 * bluetooth_manager.h - BLE UART 管理
 *
 * 使用 NimBLE 库, Nordic UART Service (NUS)
 * 设备名: "PumpCtrl-XXXX" (XXXX = MAC 后 4 位)
 * BLE UART 可与 WiFi Web UI 同时使用, 共用同一套 JSON 命令协议
 ******************************************************************************/
#ifndef BLUETOOTH_MANAGER_H
#define BLUETOOTH_MANAGER_H

#include <Arduino.h>

// 初始化 BLE (setup 中调用, 在 initWiFi 之后)
void initBluetooth();

// 处理 BLE 事件 (loop 中调用, 非阻塞)
void handleBluetooth();

// 通过 BLE 发送字符串 (供命令响应回调使用)
void bleSend(const char* data);

// BLE 是否已连接
bool bleConnected();

#endif
