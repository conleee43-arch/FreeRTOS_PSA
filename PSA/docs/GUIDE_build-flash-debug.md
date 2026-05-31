# GUIDE 构建、烧录、调试

## 支持的工作流

当前签入（提交）的代码库优先支持 Keil MDK-ARM 工作流。

主要工程文件：
- `MDK-ARM/PSA.uvprojx`

从 `MDK-ARM/PSA/PSA.build_log.htm` 观察到的工具链：
- µVision `V5.43.1.0`
- MDK-ARM Plus `5.43.0.0`
- ArmClang `V6.24`
- STM32G4 设备包 `Keil::STM32G4xx_DFP@1.5.0`
- CMSIS 包 `ARM::CMSIS@6.2.0`

## 构建步骤

1. 在 Keil µVision 中打开 `MDK-ARM/PSA.uvprojx`。
2. 选择目标 (target) `PSA`。
3. 重新构建 (Rebuild) 目标。
4. 确认输出文件已写入 `MDK-ARM/PSA/` 目录下。

预期的产物包括：
- `PSA.axf`
- `PSA.hex`
- 取决于 IDE 设置的 map/listing 相关输出
- `PSA.build_log.htm`

## 成功构建的表现

从签入的构建日志模式中预期的信号：
- 所有工程源码编译成功
- 链接步骤成功
- `FromELF` 成功创建 hex 文件
- 构建以 `0 Error(s), 0 Warning(s)` 结束

## 烧录与调试

代码库包含已签入的调试/下载相关文件：
- `MDK-ARM/JLinkSettings.ini`
- `MDK-ARM/DebugConfig/PSA_STM32G431CBTx.dbgconf`

典型工作流：
1. 在 µVision 中进行构建。
2. 启动调试会话或使用配置的下载流程。
3. 使用工程配置的 J-Link / Keil 目标设置对设备进行烧录 (Program)。

## 重要的工具链注意事项

`PSA.ioc` 包含：
- `ProjectManager.CompilerLinker=GCC`

但是，已签入的工作工程和最近成功的构建日志均指向：
- Keil MDK-ARM + ArmClang

因此：
- 将 Keil/ArmClang 视为当前代码库实际支持的工作流。
- 除非代码库被有意迁移并经过验证，否则不要将 GCC 命令作为主要路径记录在文档中。

## 内存和输出事实

来自 Keil 工程/构建输出：
- IROM start: `0x08000000`
- IROM size: `0x20000`
- IRAM start: `0x20000000`
- IRAM size: `0x8000`
- 构建日志中当前示例程序的大小：`Code=23932`, `RO-data=688`, `RW-data=36`, `ZI-data=11500`

另请参阅：
- `REF_build-config-and-memory.md`

## 何时更新此文档

在发生以下任何情况后更新本指南：
- `MDK-ARM/PSA.uvprojx` 发生更改
- 调试/下载配置发生更改
- 工具链版本发生更改
- 构建输出位置或产物发生更改
- 经证实已从优先使用 Keil 的工作流迁移到其他工作流
