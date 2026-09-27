/******************************************************************************
 * version.h — 固件版本号的唯一来源
 *
 * 两处 hello 应答 ( USB CDC 与 UART1 ) 都引用 FW_VERSION , 免得版本号在各处各写
 * 一份而互相漂移 —— hello 里的 2.3.2 曾长期落后于 README 声明的 v2.3.8 。
 *
 * 发版时同步四处 :
 *   1. 这里的 FW_VERSION
 *   2. README.md 第 3 行的版本声明
 *   3. README.md §6 更新日志新条目
 *   4. git tag v<版本号> (  annotated , 与远端一起 push )
 ******************************************************************************/
#ifndef VERSION_H
#define VERSION_H

#define FW_VERSION "2.4.0"

#endif  // VERSION_H
