# PSA 嵌入式固件 GUI 控制台

这是一个基于 Python 和 PySide6 开发的现代化串口/嵌入式控制台 GUI。它设计用于与 STM32 固件进行串口通信，承担“轻量、确定、可观测”的上位机交互职责：建立串口连接、接收固件文本行协议、刷新 ADC/内阻/状态诊断控件，并提供少量安全调试命令入口。

## 目录结构

- `main.py`: GUI 应用程序主入口，包含 `SerialReaderThread` 后台串口读取线程与 `PSAFirmwareConsole` 主窗口。
- `requirements.txt`: Python 依赖项列表。

## 快速开始

### 1. 准备环境

推荐使用 Python 虚拟环境（Virtual Environment）来运行此程序，以避免依赖冲突。

在当前 `gui` 目录下打开终端，执行以下命令：

```bash
# 创建虚拟环境
python -m venv venv

# 激活虚拟环境 (Windows)
.\venv\Scripts\activate

# 激活虚拟环境 (macOS/Linux)
source venv/bin/activate
```

### 2. 安装依赖

激活虚拟环境后，安装项目所需的库：

```bash
pip install -r requirements.txt
```

### 3. 运行程序

```bash
python main.py
```

## 当前功能特点

1. **现代化深色主题**：精心设计的扁平化、现代化深色交互界面。
2. **串口自动检索**：离线时自动扫描系统中的可用串口，连接后锁定端口选择，避免热刷新导致会话错位。
3. **ADC 实时监测**：解析固件 `[Measure]` 帧并刷新 V1、V2、VO、CO、温度与 Vref 仪表盘。
4. **内阻诊断显示**：解析 `[Calc]` 帧，显示 DCR 内阻值、3A/2A 锁存点和 IOC 观测值。
5. **保护与计算状态显示**：解析 `[State]` 帧，展示 `NORMAL`、`TRIPPED`、`RECOVERY_WAIT` 以及内阻计算状态。
6. **系统复位入口**：连接后启用 `SystemReset` 按钮，点击后发送 `SystemReset\r\n` 并清空关键诊断显示，避免 MCU 重启期间旧值误导操作者。
7. **2A 等待调试**：在 `WAIT_2A_STABLE_10S` 状态下启用“挂起 2A 等待”按钮，发送 `DebugPause2A`；收到 `WAIT_2A_PAUSED` 后切换为“恢复 2A 等待”，发送 `DebugResume2A`。
8. **手动发码终端**：底部终端可发送文本命令，并可选择十六进制显示原始接收字节；该终端用于调试兼容命令，不是主仪表盘数据源。
9. **异常自动处理**：串口异常断开后统一关闭线程与串口句柄，并复位连接态控件和 2A 调试按钮状态。

## 协议与架构真理源

- 串口文本行协议、命令字面量、正则字段顺序与状态帧全集：见 [../docs/STANDARDS_interface.md](../docs/STANDARDS_interface.md#standards-interface)。
- GUI 线程模型、控件状态收口、虚拟状态映射与运行期契约：见 [../docs/ARCH_gui-console.md](../docs/ARCH_gui-console.md#arch-gui-console)。

## 回归测试

GUI 运行态布局与交互契约由以下脚本检查：

```powershell
powershell -File ..\tests\check_gui_runtime_behavior.ps1
```

上层通信协议一致性脚本会级联运行状态机行为模型和 GUI runtime 检查：

```powershell
powershell -File ..\tests\check_uart_gui_protocol.ps1
```
