# STANDARDS_interface — 软硬件通信接口与交互协议标准

---

## 1. 遥测数据行协议 (Telemetry Line Protocol)

本项目固件通过 USART1 串口向 GUI 上位机高频广播遥测包，遥测数据采用严格的 ASCII 字符文本行进行封装，格式以 `[Measure]` 前缀标识，结尾以 `\r\n` 结尾：

```c
int measure_len = snprintf(measure_buf, sizeof(measure_buf),
                           "\r\n[Measure] V1:%0.2fV V2:%0.2fV CO:%0.2fA VO:%0.2fV T:%0.1fC Vref:%0.3fV\r\n",
                           Measure_GetV1In(),
                           Measure_GetV2In(),
                           Measure_GetCoOut(),
                           Measure_GetVoOut(),
                           Measure_GetTemp(),
                           Measure_GetVref());
```

---

## 2. 上位机捕获正则表达式标准 (MEASURE_PATTERN)

上位机 GUI 看板通过高效的工作线程，在非阻塞时间片中运用如下的高精确正则表达式捕获提取遥测参数（交流电压1峰值、交流电压2峰值、直流母线输出电流、直流母线输出电压、芯片结温、工作参考电压）：

```python
MEASURE_PATTERN = re.compile(
    r"\[Measure\]\s+V1:([\d\.-]+)V\s+V2:([\d\.-]+)V\s+CO:([\d\.-]+)A\s+VO:([\d\.-]+)V\s+T:([\d\.-]+)C\s+Vref:([\d\.-]+)V"
)
```

---

## 3. 控制指令下发协议 (SetDAC Command)

GUI 目标电流调节框和滑块（PA4 引脚理论 DAC 反馈控制）在交互触发时，向下位机固件实时推流 `SetDAC:<电流值>\r\n` 编码包，代码段规范如下：

```python
def transmit_dac_setting(self):
    if not self.ser or not self.ser.is_open:
        return

    current_val = self.dac_spin.value()
    cmd = f"SetDAC:{current_val:.2f}\r\n"
    
    try:
        self.ser.write(cmd.encode('utf-8'))
        self.status_bar.showMessage(f"已下发电流控制设定: {cmd.strip()}")
        self.append_log(f">>> 发送指令: {cmd.strip()}", is_system=True)
        self.last_dac_current = current_val
    except Exception as e:
        self.status_bar.showMessage(f"下发设定失败: {str(e)}")
```

---

## 4. 保护使能控制指令 (SetOF Command)

GUI 过流保护使能按钮在点击时，向下位机固件实时推流 SetOF:<0/1>\r\n 指令包，控制 PB0 引脚状态：
- SetOF:1\r\n 代表将 PB0 (OF_EN) 引脚电平拉高。
- SetOF:0\r\n 代表将 PB0 (OF_EN) 引脚电平拉低。

---

## 5. 协议边界限制与闭环校验标准

为保障在 115200bps 甚至更低物理波特率下传输不发生粘包与 DMA 搬运溢出错误，串口交互格式受到以下强约束限制：

- **单包物理长度限制**：遥测包长度在最恶劣极限状况下（负温度、四位电压数），也严禁超过固件的 `UART_DMA_TX_ITEM_MAX_LEN` 限制（128 字节）。
- **帧独立分离原则**：遥测主数据帧 `[Measure]` 和诊断码帧 `[Code]` 必须放在各自独立的物理 DMA 帧中由硬件推送，严禁合并在一次 `snprintf` 格式化传输中。

此通信协议的闭环一致性由自动化断言脚本 [check_uart_gui_protocol.ps1](file:///d:/zhihai/Software/FreeRTOS_PSA/tests/check_uart_gui_protocol.ps1) 强制保证，在任何接口发生细微变动时，必须通过如下的自动化文本断言：

```powershell
$expectedFirmwareFormat = '[Measure] V1:%0.2fV V2:%0.2fV CO:%0.2fA VO:%0.2fV T:%0.1fC Vref:%0.3fV'
if (-not $firmware.Contains($expectedFirmwareFormat)) {
    throw "Firmware UART output does not contain GUI-compatible format: $expectedFirmwareFormat"
}
```
