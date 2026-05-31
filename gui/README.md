# PSA 嵌入式固件 GUI 控制台

这是一个基于 Python 和 PySide6 开发的现代化串口/嵌入式控制台 GUI。它设计用于与 STM32 固件进行串口通信，具备现代化的 UI 设计、串口参数配置以及实时数据收发和图表可视化扩展接口。

## 目录结构

- `main.py`: GUI 应用程序主入口。
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

## 功能特点

1. **现代化深色主题**：精心设计的扁平化、现代化深色交互界面。
2. **串口自动检索**：自动扫描系统中的可用串口，支持波特率等参数设置。
3. **数据实时收发**：支持文本和十六进制（Hex）收发模式，支持显示时间戳。
4. **异常自动处理**：串口异常断开自动挂起并提示，防止程序卡死。
