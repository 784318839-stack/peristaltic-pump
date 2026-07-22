# 硬件设计文件

## PCB
电路板设计文件（原理图、PCB 布局、Gerber 文件等）

### 版本历史
| 版本 | 日期 | 文件 | 说明 |
|------|------|------|------|
| v1.0 | 2026-07-23 | `Altium_PCB_2026-07-23.zip` | 第一版 PCB 设计（Altium Designer）— 原理图 `P1.schdoc` + PCB `PCB1.pcbdoc` |

### 文件结构
```
pcb/
├── Altium_PCB_2026-07-23.zip   # 第一版设计打包
└── PCB/Board1/
    ├── PCB1.pcbdoc              # PCB 版图
    └── Schematic1/
        └── P1.schdoc            # 原理图
```

## 模型
3D 打印外壳、支架等模型文件（STL、STEP 等）
