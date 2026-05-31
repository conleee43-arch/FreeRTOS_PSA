# REF 构建配置与内存

## 构建配置事实

主要签入（提交）的构建工程：
- `MDK-ARM/PSA.uvprojx`

观察到的成功构建证据：
- `MDK-ARM/PSA/PSA.build_log.htm`

## 工具链事实

来自当前构建日志：
- µVision: `V5.43.1.0`
- MDK-ARM Plus: `5.43.0.0`
- ArmClang: `V6.24`
- 汇编器 (Assembler): `Armasm.exe V6.24`
- 链接器 (Linker): `ArmLink.exe V6.24`
- Hex 转换器: `FromElf.exe V6.24`

## 设备与支持包事实

来自 Keil 工程和构建日志：
- 设备 (Device): `STM32G431CBTx`
- 设备包 (Device pack): `Keil::STM32G4xx_DFP@1.5.0`
- CMSIS 包: `ARM::CMSIS@6.2.0`
- 包含的 CMSIS 核心组件版本: `6.1.1`

## 内存布局

来自 `MDK-ARM/PSA.uvprojx`：
- `IROM` 起始地址: `0x08000000`
- `IROM` 大小: `0x20000`
- `IRAM` 起始地址: `0x20000000`
- `IRAM` 大小: `0x8000`

## 输出产物

来自 Keil 目标设置和构建日志：
- 输出目录: `MDK-ARM/PSA/`
- 可执行文件: `PSA.axf`
- Hex 文件生成: 已启用
- 典型的 hex 输出: `PSA.hex`
- Map/browse/debug 信息: 已通过工程配置启用

## 当前占用空间 (footprint) 快照

来自 `MDK-ARM/PSA/PSA.build_log.htm`：
- Code (代码段): `23932`
- RO-data (只读数据段): `688`
- RW-data (读写数据段): `36`
- ZI-data (零初始化数据段): `11500`

这只是一个快照，并非保证的长期限制。

## 重要的工作流注意事项

CubeMX 元数据当前显示：
- `ProjectManager.CompilerLinker=GCC`

但是，已签入的实际构建路径是：
- Keil MDK-ARM + ArmClang

除非代码库被有意迁移，否则请将 Keil 工程和构建日志视为实际运行的工作流。

## 常用文件位置

- Keil 工程: `MDK-ARM/PSA.uvprojx`
- 构建日志: `MDK-ARM/PSA/PSA.build_log.htm`
- 调试配置: `MDK-ARM/DebugConfig/PSA_STM32G431CBTx.dbgconf`
- J-Link 设置: `MDK-ARM/JLinkSettings.ini`
- CubeMX 配置: `PSA.ioc`

## 何时更新此文档

在发生以下更改后进行更新：
- 工具链版本
- 设备包 / CMSIS 包
- 内存布局
- 输出产物的命名或路径
- 当前的占用空间快照（当大小基准变得重要时）
