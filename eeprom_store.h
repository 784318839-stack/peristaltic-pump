/******************************************************************************
 * eeprom_store.h — EEPROM 持久化 & 方案预设管理
 *
 * 参数修改时 markDirty() , 空闲 / 完成时 saveParams() 统一写入
 * 避免频繁擦写 Flash ( ESP32 Flash 寿命约 10 万次擦除 )
 *
 * EEPROM 布局 ( 512 字节 ) 见 eeprom_store.cpp 头部注释
 ******************************************************************************/
#ifndef EEPROM_STORE_H
#define EEPROM_STORE_H

#include <Arduino.h>

// 标记参数已修改 ( 延迟写入 )
void markDirty();

// 写入 EEPROM ( 仅在 markDirty 后执行 )
void saveParams();

// 从 EEPROM 加载参数 , 返回 false = 首次上电 / 数据损坏
bool loadParams();

// 方案预设 ( 4 槽位 )
bool isPresetValid( int slot );
void savePreset( int slot );
void loadPreset( int slot );

#endif
