import sys
import re
import time
import serial
import serial.tools.list_ports
from datetime import datetime
from PySide6.QtCore import QThread, Signal, Slot, Qt, QTimer, QPointF
from PySide6.QtWidgets import (
    QApplication,
    QMainWindow,
    QWidget,
    QVBoxLayout,
    QHBoxLayout,
    QGridLayout,
    QLabel,
    QComboBox,
    QPushButton,
    QTextEdit,
    QLineEdit,
    QCheckBox,
    QStatusBar,
    QGroupBox,
    QSlider,
    QDoubleSpinBox,
    QSplitter,
    QFrame
)
from PySide6.QtGui import QFont, QIcon, QTextCursor, QPainter, QPen, QColor, QBrush, QLinearGradient, QPolygonF

# 正则表达式：精准捕获固件输出的测量数据
# 格式: [Measure] V1:220.00V V2:0.00V CO:1.25A VO:380.00V T:35.5C Vref:3.298V
MEASURE_PATTERN = re.compile(
    r"\[Measure\]\s+V1:([\d\.-]+)V\s+V2:([\d\.-]+)V\s+CO:([\d\.-]+)A\s+VO:([\d\.-]+)V\s+T:([\d\.-]+)C\s+Vref:([\d\.-]+)V"
)

# 正则表达式：解析内阻计算报告
# 格式: [Calc] R:2.000R U1:400.00V I1:3.00A U2:398.00V I2:2.00A IOC:12.00A
CALC_PATTERN = re.compile(
    r"\[Calc\]\s+R:([\d\.-]+)R\s+U1:([\d\.-]+)V\s+I1:([\d\.-]+)A\s+U2:([\d\.-]+)V\s+I2:([\d\.-]+)A\s+IOC:([\d\.-]+)A"
)

# 正则表达式：解析保护/内阻状态机状态转移帧
# 格式: [State] STATE:NORMAL
STATE_PATTERN = re.compile(
    r"\[State\]\s+STATE:([A-Za-z0-9_]+)"
)

# QSS 高保真扁平化深色视觉主题
MODERN_STYLE = """
QMainWindow {
    background-color: #0d0d12;
}

QWidget {
    color: #cbd5e1;
    font-family: 'Segoe UI', -apple-system, BlinkMacSystemFont, Roboto, sans-serif;
    font-size: 13px;
}

/* 卡片式分组框 */
QGroupBox {
    border: 1px solid #1e1e2f;
    border-radius: 10px;
    margin-top: 15px;
    font-weight: bold;
    color: #818cf8;
    background-color: #13131f;
}

QGroupBox::title {
    subcontrol-origin: margin;
    subcontrol-position: top left;
    left: 15px;
    padding: 0 8px;
}

/* 顶部与边栏通用标签 */
QLabel {
    color: #94a3b8;
}

/* 状态数码字 */
QLabel#ValLabel {
    font-family: 'Consolas', monospace;
    font-size: 26px;
    font-weight: bold;
    color: #38bdf8;
    background-color: #09090e;
    border: 1px solid #1e1e2f;
    border-radius: 6px;
    padding: 5px;
}

/* 下拉选择框 */
QComboBox {
    background-color: #1e1e2f;
    border: 1px solid #334155;
    border-radius: 6px;
    padding: 6px 10px;
    color: #f1f5f9;
}

QComboBox:hover {
    border: 1px solid #6366f1;
}

/* 按钮设计 */
QPushButton {
    background: qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:1, stop:0 #6366f1, stop:1 #4f46e5);
    border: none;
    border-radius: 6px;
    color: white;
    padding: 8px 16px;
    font-weight: bold;
}

QPushButton:hover {
    background: qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:1, stop:0 #4f46e5, stop:1 #4338ca);
}

QPushButton:pressed {
    background: #3730a3;
}

QPushButton:disabled {
    background: #1e1e2f;
    color: #475569;
}

QPushButton#disconnectBtn {
    background: qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:1, stop:0 #ef4444, stop:1 #dc2626);
}

QPushButton#disconnectBtn:hover {
    background: qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:1, stop:0 #dc2626, stop:1 #b91c1c);
}

/* 双向微调框 */
QDoubleSpinBox {
    background-color: #1e1e2f;
    border: 1px solid #334155;
    border-radius: 6px;
    padding: 5px;
    color: #f1f5f9;
    font-weight: bold;
    font-size: 15px;
}

QDoubleSpinBox:focus {
    border: 1px solid #6366f1;
}

/* 滑块样式 */
QSlider::groove:horizontal {
    border: 1px solid #1e1e2f;
    height: 8px;
    background: #09090e;
    margin: 2px 0;
    border-radius: 4px;
}

QSlider::handle:horizontal {
    background: qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:1, stop:0 #818cf8, stop:1 #6366f1);
    border: none;
    width: 18px;
    height: 18px;
    margin: -5px 0;
    border-radius: 9px;
}

QSlider::handle:horizontal:hover {
    background: #4f46e5;
}

/* 文本和日志终端区 */
QTextEdit, QLineEdit {
    background-color: #07070a;
    border: 1px solid #1e1e2f;
    border-radius: 8px;
    padding: 8px;
    color: #f8fafc;
    font-family: 'Consolas', 'Courier New', monospace;
}

/* 状态栏 */
QStatusBar {
    background-color: #07070a;
    color: #64748b;
    border-top: 1px solid #13131f;
}
"""

class RealTimeTrendPlot(QWidget):
    """
    使用 QPainter 编写的超流畅、科技感实时趋势图
    用于显示实测电流(CO_OUT)和实测电压(VO_OUT)的波形曲线
    """
    def __init__(self, parent=None):
        super().__init__(parent)
        self.max_points = 100
        self.current_data = [0.0] * self.max_points
        self.voltage_data = [0.0] * self.max_points

        self.setMinimumHeight(180)

    def append_data(self, current: float, voltage: float):
        """向数据队列中追加数据并触发重绘"""
        self.current_data.pop(0)
        self.current_data.append(current)

        self.voltage_data.pop(0)
        self.voltage_data.append(voltage)
        self.update()

    def clear_data(self):
        """重置数据"""
        self.current_data = [0.0] * self.max_points
        self.voltage_data = [0.0] * self.max_points
        self.update()

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.Antialiasing)

        width = self.width()
        height = self.height()

        # 1. 绘制网格背景
        bg_brush = QBrush(QColor(11, 11, 18))
        painter.fillRect(0, 0, width, height, bg_brush)

        # 绘制网格虚线
        grid_pen = QPen(QColor(30, 30, 45), 1, Qt.DashLine)
        painter.setPen(grid_pen)
        rows = 5
        for i in range(1, rows):
            y = int(height * i / rows)
            painter.drawLine(40, y, width - 40, y)

        cols = 10
        for i in range(1, cols):
            x = int(40 + (width - 80) * i / cols)
            painter.drawLine(x, 10, x, height - 20)

        # 2. 计算缩放边界
        # 电流 (0 ~ 15A) | 电压 (0 ~ 500V)
        max_i, min_i = 15.0, 0.0
        max_v, min_v = 500.0, 0.0

        # 如果数据超出预设，动态微调
        data_max_i = max(self.current_data)
        if data_max_i > max_i:
            max_i = data_max_i * 1.2

        data_max_v = max(self.voltage_data)
        if data_max_v > max_v:
            max_v = data_max_v * 1.2

        # 绘制坐标轴两端的最大值提示
        text_pen = QPen(QColor(148, 163, 184))
        painter.setPen(text_pen)
        painter.setFont(QFont("Consolas", 8))
        painter.drawText(5, 15, f"{max_i:.1f}A")
        painter.drawText(width - 35, 15, f"{max_v:.0f}V")
        painter.drawText(5, height - 10, "0A")
        painter.drawText(width - 25, height - 10, "0V")

        # 3. 绘制电流折线 (亮青色，带半透明渐变区域)
        i_poly = QPolygonF()
        i_points = []
        x_start = 40
        x_end = width - 40
        x_step = (x_end - x_start) / (self.max_points - 1)

        # 头部压入起点，用于封闭渐变色多边形
        i_poly.append(QPointF(x_start, height - 20))

        for idx, val in enumerate(self.current_data):
            x = x_start + idx * x_step
            # 缩放映射 y
            norm_val = (val - min_i) / (max_i - min_i) if (max_i - min_i) > 0 else 0
            y = height - 20 - norm_val * (height - 30)
            pt = QPointF(x, y)
            i_points.append(pt)
            i_poly.append(pt)

        i_poly.append(QPointF(x_end, height - 20))

        # 绘制电流渐变填充区域
        i_grad = QLinearGradient(0, 0, 0, height)
        i_grad.setColorAt(0.0, QColor(6, 182, 212, 60))
        i_grad.setColorAt(1.0, QColor(6, 182, 212, 0))
        painter.setBrush(QBrush(i_grad))
        painter.setPen(Qt.NoPen)
        painter.drawPolygon(i_poly)

        # 绘制电流亮色边缘折线
        i_line_pen = QPen(QColor(6, 182, 212), 2, Qt.SolidLine)
        painter.setPen(i_line_pen)
        for k in range(len(i_points) - 1):
            painter.drawLine(i_points[k], i_points[k+1])

        # 4. 绘制电压折线 (亮橙色，带半透明渐变区域)
        v_poly = QPolygonF()
        v_points = []
        v_poly.append(QPointF(x_start, height - 20))

        for idx, val in enumerate(self.voltage_data):
            x = x_start + idx * x_step
            norm_val = (val - min_v) / (max_v - min_v) if (max_v - min_v) > 0 else 0
            y = height - 20 - norm_val * (height - 30)
            pt = QPointF(x, y)
            v_points.append(pt)
            v_poly.append(pt)

        v_poly.append(QPointF(x_end, height - 20))

        # 绘制电压渐变填充区域
        v_grad = QLinearGradient(0, 0, 0, height)
        v_grad.setColorAt(0.0, QColor(249, 115, 22, 50))
        v_grad.setColorAt(1.0, QColor(249, 115, 22, 0))
        painter.setBrush(QBrush(v_grad))
        painter.setPen(Qt.NoPen)
        painter.drawPolygon(v_poly)

        # 绘制电压亮色边缘折线
        v_line_pen = QPen(QColor(249, 115, 22), 2, Qt.SolidLine)
        painter.setPen(v_line_pen)
        for k in range(len(v_points) - 1):
            painter.drawLine(v_points[k], v_points[k+1])

        # 5. 绘制图例
        painter.setFont(QFont("Segoe UI", 9))
        painter.setPen(QPen(QColor(6, 182, 212)))
        painter.drawText(50, 25, "■ 实测输出电流 (CO_OUT)")
        painter.setPen(QPen(QColor(249, 115, 22)))
        painter.drawText(220, 25, "■ 实测输出电压 (VO_OUT)")


class SerialReaderThread(QThread):
    """
    串口接收工作线程，保持后台安全读取，数据通过 Signal 抛给 UI 主线程
    """
    line_received = Signal(str)
    raw_data_received = Signal(bytes)
    error_occurred = Signal(str)

    def __init__(self, ser: serial.Serial):
        super().__init__()
        self.ser = ser
        self.running = False
        self.line_buffer = bytearray()

    def run(self):
        self.running = True
        while self.running:
            try:
                if self.ser and self.ser.is_open:
                    if self.ser.in_waiting > 0:
                        data = self.ser.read(self.ser.in_waiting)
                        if data:
                            self.raw_data_received.emit(data)

                            # 按照行协议进行解包（\n 结尾分割）
                            self.line_buffer.extend(data)
                            while b'\n' in self.line_buffer:
                                idx = self.line_buffer.index(b'\n')
                                raw_line = self.line_buffer[:idx+1]
                                self.line_buffer = self.line_buffer[idx+1:]

                                # 解码单行数据
                                try:
                                    line_str = raw_line.decode('utf-8', errors='replace').strip()
                                    if line_str:
                                        self.line_received.emit(line_str)
                                except Exception:
                                    pass
                time.sleep(0.005)
            except serial.SerialException as e:
                self.error_occurred.emit(f"串口异常中断: {str(e)}")
                self.running = False
            except Exception as e:
                self.error_occurred.emit(f"未知异常: {str(e)}")
                self.running = False

    def stop(self):
        self.running = False
        self.wait()


class PSAFirmwareConsole(QMainWindow):
    def __init__(self):
        super().__init__()
        self.ser = None
        self.reader_thread = None
        self.last_dac_current = 0.00  # 固件开机默认安全关断输出

        self.init_ui()
        self.setup_connections()

        # 端口检测定时器
        self.port_timer = QTimer(self)
        self.port_timer.timeout.connect(self.scan_ports)
        self.port_timer.start(2000)
        self.scan_ports()

    def init_ui(self):
        self.setWindowTitle("PSA 嵌入式固件智能控制看板")
        self.resize(900, 650)
        self.setStyleSheet(MODERN_STYLE)

        central_widget = QWidget(self)
        self.setCentralWidget(central_widget)

        # 顶层整体垂直布局
        main_layout = QVBoxLayout(central_widget)
        main_layout.setContentsMargins(15, 15, 15, 15)
        main_layout.setSpacing(15)

        # ==================== 左侧控制侧边栏 ====================
        left_panel = QVBoxLayout()
        left_panel.setSpacing(12)

        # 1. 串口配置卡片
        port_group = QGroupBox("串口通信连接", central_widget)
        port_layout = QHBoxLayout(port_group)
        port_layout.setContentsMargins(15, 12, 15, 12)
        port_layout.setSpacing(12)

        port_layout.addWidget(QLabel("串口端口:", port_group))
        self.port_combo = QComboBox(port_group)
        self.port_combo.setMinimumWidth(150)
        port_layout.addWidget(self.port_combo)

        port_layout.addWidget(QLabel("波特率:", port_group))
        self.baud_combo = QComboBox(port_group)
        self.baud_combo.addItems(["9600", "19200", "38400", "57600", "115200", "230400", "460800", "921600"])
        self.baud_combo.setCurrentText("115200")
        self.baud_combo.setMinimumWidth(100)
        port_layout.addWidget(self.baud_combo)

        self.connect_btn = QPushButton("开启控制台", port_group)
        port_layout.addWidget(self.connect_btn)

        self.disconnect_btn = QPushButton("关闭控制台", port_group)
        self.disconnect_btn.setObjectName("disconnectBtn")
        self.disconnect_btn.setEnabled(False)
        port_layout.addWidget(self.disconnect_btn)

        port_layout.addStretch(1)

        # 2. DAC 设定与闭环监控卡片 (核心改进)
        dac_group = QGroupBox("DAC 目标电流设定 (PA4)", central_widget)
        dac_layout = QVBoxLayout(dac_group)
        dac_layout.setContentsMargins(15, 20, 15, 15)
        dac_layout.setSpacing(12)

        # 数字设定与反馈对比
        spin_layout = QHBoxLayout()
        spin_layout.addWidget(QLabel("目标电流 (A):", dac_group))
        self.dac_spin = QDoubleSpinBox(dac_group)
        self.dac_spin.setRange(0.00, 10.00)
        self.dac_spin.setSingleStep(0.1)
        self.dac_spin.setValue(0.00)
        spin_layout.addWidget(self.dac_spin)
        dac_layout.addLayout(spin_layout)

        # 滑动条设置
        self.dac_slider = QSlider(Qt.Horizontal, dac_group)
        self.dac_slider.setRange(0, 1000)  # 0.00A 到 10.00A，乘 100 映射
        self.dac_slider.setValue(0)
        dac_layout.addWidget(self.dac_slider)

        # DAC 寄存器数字值提示
        self.lbl_dac_code = QLabel("理论 DAC 数字量: 0  (0.000 V)", dac_group)
        self.lbl_dac_code.setStyleSheet("color: #a7f3d0; font-family: Consolas;")
        dac_layout.addWidget(self.lbl_dac_code)

        # 电流闭环指标对比
        comp_frame = QFrame(dac_group)
        comp_frame.setStyleSheet("background-color: #09090e; border: 1px solid #1e1e2f; border-radius: 6px;")
        comp_layout = QGridLayout(comp_frame)
        comp_layout.setContentsMargins(8, 8, 8, 8)

        comp_layout.addWidget(QLabel("给出的 DAC 设定:", comp_frame), 0, 0)
        self.lbl_dac_set = QLabel("0.00 A", comp_frame)
        self.lbl_dac_set.setStyleSheet("color: #6366f1; font-weight: bold; font-family: Consolas;")
        comp_layout.addWidget(self.lbl_dac_set, 0, 1)

        comp_layout.addWidget(QLabel("反馈的 ADC 实测:", comp_frame), 1, 0)
        self.lbl_adc_fb = QLabel("0.00 A", comp_frame)
        self.lbl_adc_fb.setStyleSheet("color: #06b6d4; font-weight: bold; font-family: Consolas;")
        comp_layout.addWidget(self.lbl_adc_fb, 1, 1)

        dac_layout.addWidget(comp_frame)

        # 控制指令下发按钮
        self.send_dac_btn = QPushButton("下发设定 (SetDAC)", dac_group)
        self.send_dac_btn.setEnabled(False)
        dac_layout.addWidget(self.send_dac_btn)

        # OF_EN 状态展示
        status_row = QHBoxLayout()
        status_row.addWidget(QLabel("OF_EN 物理状态:", dac_group))
        self.lbl_of_status = QLabel("⚪ 离线状态 (OFFLINE)", dac_group)
        self.lbl_of_status.setStyleSheet("color: #64748b; font-weight: bold; font-family: Segoe UI; font-size: 13px;")
        status_row.addWidget(self.lbl_of_status)
        status_row.addStretch(1)
        dac_layout.addLayout(status_row)

        # OF_EN 引脚状态控制双联按钮
        of_en_layout = QHBoxLayout()
        self.btn_of_en_on = QPushButton("使能 (OF_EN = 1)", dac_group)
        self.btn_of_en_on.setEnabled(False)
        self.btn_of_en_off = QPushButton("禁用 (OF_EN = 0)", dac_group)
        self.btn_of_en_off.setEnabled(False)
        of_en_layout.addWidget(self.btn_of_en_on)
        of_en_layout.addWidget(self.btn_of_en_off)
        dac_layout.addLayout(of_en_layout)

        # 固件接口提示
        lbl_tip = QLabel("提示：固件默认安全关断输出。采样就绪且保护状态正常时，可通过 GUI 发送 'SetDAC:x.xx' 控制指令动态设定。", dac_group)
        lbl_tip.setWordWrap(True)
        lbl_tip.setStyleSheet("font-size: 11px; color: #64748b;")
        dac_layout.addWidget(lbl_tip)

        # left_panel.addWidget(dac_group)

        # left_panel.addWidget(diag_group)

        # left_panel.addStretch(1)

        # ==================== 右侧数据看板交互区 ====================
        right_panel = QVBoxLayout()
        right_panel.setSpacing(12)

        # 1. ADC 物理参数核心指标监测看板 (5个主要通道)
        adc_group = QGroupBox("ADC 物理参数实时测量监测舱", central_widget)
        adc_layout = QGridLayout(adc_group)
        adc_layout.setContentsMargins(15, 20, 15, 15)
        adc_layout.setSpacing(12)

        # 1.1 V1_IN 交流输入 1
        v1_box = QVBoxLayout()
        v1_box.addWidget(QLabel("V1_IN 交流输入 1", adc_group))
        self.lbl_v1_val = QLabel("0.00 V", adc_group)
        self.lbl_v1_val.setObjectName("ValLabel")
        self.lbl_v1_val.setAlignment(Qt.AlignCenter)
        v1_box.addWidget(self.lbl_v1_val)
        adc_layout.addLayout(v1_box, 0, 0)

        # 1.2 V2_IN 交流输入 2
        v2_box = QVBoxLayout()
        v2_box.addWidget(QLabel("V2_IN 交流输入 2", adc_group))
        self.lbl_v2_val = QLabel("0.00 V", adc_group)
        self.lbl_v2_val.setObjectName("ValLabel")
        self.lbl_v2_val.setAlignment(Qt.AlignCenter)
        v2_box.addWidget(self.lbl_v2_val)
        adc_layout.addLayout(v2_box, 0, 1)

        # 1.3 VO_OUT 直流高压输出
        vo_box = QVBoxLayout()
        vo_box.addWidget(QLabel("VO_OUT 直流高压输出", adc_group))
        self.lbl_vo_val = QLabel("0.00 V", adc_group)
        self.lbl_vo_val.setObjectName("ValLabel")
        self.lbl_vo_val.setAlignment(Qt.AlignCenter)
        self.lbl_vo_val.setStyleSheet("color: #f97316;")  # 橙色代表高压危险警告色
        vo_box.addWidget(self.lbl_vo_val)
        adc_layout.addLayout(vo_box, 1, 1)

        # 1.4 CO_OUT 直流输出电流
        co_box = QVBoxLayout()
        co_box.addWidget(QLabel("CO_OUT 直流输出电流", adc_group))
        self.lbl_co_val = QLabel("0.00 A", adc_group)
        self.lbl_co_val.setObjectName("ValLabel")
        self.lbl_co_val.setAlignment(Qt.AlignCenter)
        self.lbl_co_val.setStyleSheet("color: #10b981;")  # 绿色代表电流负载正常
        co_box.addWidget(self.lbl_co_val)
        adc_layout.addLayout(co_box, 1, 0)

        # 1.5 MCU 片上温度传感器
        temp_box = QVBoxLayout()
        temp_box.addWidget(QLabel("MCU 核心温度", adc_group))
        self.lbl_temp_val = QLabel("0.00 °C", adc_group)
        self.lbl_temp_val.setObjectName("ValLabel")
        self.lbl_temp_val.setAlignment(Qt.AlignCenter)
        temp_box.addWidget(self.lbl_temp_val)
        adc_layout.addLayout(temp_box, 0, 2)

        # 1.6 Vref 内部参考电压
        vref_box = QVBoxLayout()
        vref_box.addWidget(QLabel("VREF+ 内部参考电压", adc_group))
        self.lbl_vref_val = QLabel("0.000 V", adc_group)
        self.lbl_vref_val.setObjectName("ValLabel")
        self.lbl_vref_val.setAlignment(Qt.AlignCenter)
        vref_box.addWidget(self.lbl_vref_val)
        adc_layout.addLayout(vref_box, 1, 2)

        right_panel.addWidget(adc_group)

        # 2. 实时趋势波形图
        trend_group = QGroupBox("输出电流 (CO_OUT) 与电压 (VO_OUT) 实时分析曲线", central_widget)
        trend_layout = QVBoxLayout(trend_group)
        trend_layout.setContentsMargins(10, 20, 10, 10)
        self.trend_plot = RealTimeTrendPlot(trend_group)
        trend_layout.addWidget(self.trend_plot)
        # right_panel.addWidget(trend_group)

        # 3. 原始数据与调试日志 (可折叠的控制终端)
        terminal_group = QGroupBox("底层串口通信终端监视器", central_widget)
        term_layout = QVBoxLayout(terminal_group)
        term_layout.setContentsMargins(15, 20, 15, 15)

        self.txt_receive = QTextEdit(terminal_group)
        self.txt_receive.setReadOnly(True)
        self.txt_receive.setMaximumHeight(100)  # 底层日志限制高度，将空间留给仪表盘
        term_layout.addWidget(self.txt_receive)

        # 控制指令发码区 (不加入布局显示)
        send_row = QHBoxLayout()
        self.txt_send = QLineEdit(terminal_group)
        self.txt_send.setPlaceholderText("在此处输入手动控制指令（如 Start / Stop / ReSet System）...")
        self.send_btn = QPushButton("发送命令", terminal_group)
        self.send_btn.setEnabled(False)
        send_row.addWidget(self.txt_send, stretch=4)
        send_row.addWidget(self.send_btn, stretch=1)
        # term_layout.addLayout(send_row)

        chk_row = QHBoxLayout()
        self.chk_hex_show = QCheckBox("十六进制 (Hex Show)", terminal_group)
        self.chk_auto_scroll = QCheckBox("自动滚动", terminal_group)
        self.chk_auto_scroll.setChecked(True)
        self.clear_btn = QPushButton("清空终端", terminal_group)
        self.clear_btn.setStyleSheet("background-color: #334155; padding: 4px 10px;")

        chk_row.addWidget(self.chk_hex_show)
        chk_row.addWidget(self.chk_auto_scroll)
        chk_row.addStretch(1)
        chk_row.addWidget(self.clear_btn)
        term_layout.addLayout(chk_row)

        right_panel.addWidget(terminal_group)

        # 主界面集成布局
        main_layout.addWidget(port_group)
        main_layout.addWidget(adc_group)
        main_layout.addStretch(1)
        main_layout.addWidget(terminal_group)

        # 状态栏
        self.status_bar = QStatusBar(self)
        self.setStatusBar(self.status_bar)
        self.status_bar.showMessage("初始化就绪。请选择串口以启用监测舱。")

    def setup_connections(self):
        """绑定信号、槽以及各种控件变动"""
        self.connect_btn.clicked.connect(self.open_serial)
        self.disconnect_btn.clicked.connect(self.close_serial)
        self.clear_btn.clicked.connect(self.clear_receive_area)
        self.send_btn.clicked.connect(self.send_command)
        self.txt_send.returnPressed.connect(self.send_command)

        # DAC 设定输入联动
        self.dac_spin.valueChanged.connect(self.sync_spin_to_slider)
        self.dac_slider.valueChanged.connect(self.sync_slider_to_spin)
        self.send_dac_btn.clicked.connect(self.transmit_dac_setting)

        # OF_EN 保护控制联动
        self.btn_of_en_on.clicked.connect(lambda: self.transmit_of_en_setting(1))
        self.btn_of_en_off.clicked.connect(lambda: self.transmit_of_en_setting(0))

    def scan_ports(self):
        """扫描可用端口"""
        if self.ser and self.ser.is_open:
            return

        ports = serial.tools.list_ports.comports()
        current_ports = [self.port_combo.itemText(i) for i in range(self.port_combo.count())]
        new_ports = [f"{p.device} ({p.description})" for p in ports]

        if sorted(current_ports) != sorted(new_ports):
            self.port_combo.clear()
            for p in ports:
                self.port_combo.addItem(f"{p.device} ({p.description})", p.device)

            if ports:
                self.status_bar.showMessage(f"扫描完成，发现 {len(ports)} 个可用端口。")
            else:
                self.status_bar.showMessage("未检测到串口，请检查硬件连接与虚拟串口驱动。")

    def sync_spin_to_slider(self, val: float):
        """数字调节框改变时同步更新滑块和理论 DAC 码值计算"""
        # 阻断递归信号
        self.dac_slider.blockSignals(True)
        self.dac_slider.setValue(int(val * 100))
        self.dac_slider.blockSignals(False)
        self.update_dac_calculations(val)

    def sync_slider_to_spin(self, val_int: int):
        """滑块滑动时同步更新数字调节框和理论 DAC 码值计算"""
        val_float = val_int / 100.0
        self.dac_spin.blockSignals(True)
        self.dac_spin.setValue(val_float)
        self.dac_spin.blockSignals(False)
        self.update_dac_calculations(val_float)

    def update_dac_calculations(self, current: float):
        """根据电流计算理论数字量和电压并刷新提示标签"""
        # 公式: DAC_Code = Current * 409.5
        dac_code = int((current * 409.5) + 0.5)
        if dac_code > 4095:
            dac_code = 4095

        # 理论电压 (DAC参考 2.5V): V_out = (DAC_Code / 4095) * 2.5V
        v_out = (dac_code / 4095.0) * 2.5

        self.lbl_dac_code.setText(f"理论 DAC 数字量: {dac_code}  ({v_out:.3f} V)")
        self.lbl_dac_set.setText(f"{current:.2f} A")

    def open_serial(self):
        """打开串口，建立后台读取线程"""
        if self.port_combo.count() == 0:
            self.status_bar.showMessage("错误: 无可用物理串口！")
            return

        port = self.port_combo.currentData()
        baudrate = int(self.baud_combo.currentText())

        try:
            self.ser = serial.Serial(
                port=port,
                baudrate=baudrate,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE,
                bytesize=serial.EIGHTBITS,
                timeout=0.1
            )

            # 启动专属行接收线程
            self.reader_thread = SerialReaderThread(self.ser)
            self.reader_thread.line_received.connect(self.handle_line_received)
            self.reader_thread.raw_data_received.connect(self.handle_raw_data)
            self.reader_thread.error_occurred.connect(self.handle_serial_error)
            self.reader_thread.start()

            self.ui_state_connected(True)
            self.status_bar.showMessage(f"控制台已开启，已连接 {port} ({baudrate} bps)")
            self.append_log(f"--- 开启串口连接 {port} ---", is_system=True)

        except serial.SerialException as e:
            self.status_bar.showMessage(f"开启串口失败: {str(e)}")

    def close_serial(self):
        """优雅关闭串口与数据线程"""
        if self.reader_thread:
            self.reader_thread.stop()
            self.reader_thread = None

        if self.ser and self.ser.is_open:
            try:
                self.ser.close()
            except Exception:
                pass
        self.ser = None
        self.ui_state_connected(False)
        self.status_bar.showMessage("控制台已关闭。")
        self.append_log("--- 关闭串口连接 ---", is_system=True)

    def ui_state_connected(self, connected: bool):
        self.port_combo.setEnabled(not connected)
        self.baud_combo.setEnabled(not connected)
        self.connect_btn.setEnabled(not connected)

        self.disconnect_btn.setEnabled(connected)
        self.send_btn.setEnabled(connected)
        self.send_dac_btn.setEnabled(connected)
        self.btn_of_en_on.setEnabled(connected)
        self.btn_of_en_off.setEnabled(connected)

        if connected:
            self.update_of_en_ui_styles(0)
        else:
            self.update_of_en_ui_styles(-1)

    @Slot(str)
    def handle_line_received(self, line: str):
        """行协议解析槽：在此处实时拦截固件的 Measure 物理输出行并提取数据"""
        self.append_log(line, is_system=False)

        # 匹配正则：V1_IN, V2_IN, CO_OUT, VO_OUT, Temp, Vref
        match = MEASURE_PATTERN.search(line)
        if match:
            try:
                v1_in = float(match.group(1))
                v2_in = float(match.group(2))
                co_out = float(match.group(3))
                vo_out = float(match.group(4))
                temp_c = float(match.group(5))
                vref = float(match.group(6))

                # 刷新 ADC 卡片显示
                self.lbl_v1_val.setText(f"{v1_in:.2f} V")
                self.lbl_v2_val.setText(f"{v2_in:.2f} V")
                self.lbl_vo_val.setText(f"{vo_out:.2f} V")
                self.lbl_co_val.setText(f"{co_out:.2f} A")
                self.lbl_temp_val.setText(f"{temp_c:.2f} °C")
                self.lbl_vref_val.setText(f"{vref:.3f} V")

                # 反馈至 DAC 闭环监视指示框
                self.lbl_adc_fb.setText(f"{co_out:.2f} A")

                # 动态刷新趋势折线图
                self.trend_plot.append_data(co_out, vo_out)

            except ValueError:
                pass

        # 匹配内阻计算报告
        calc_match = CALC_PATTERN.search(line)
        if calc_match:
            try:
                r_val = float(calc_match.group(1))
                u1_val = float(calc_match.group(2))
                i1_val = float(calc_match.group(3))
                u2_val = float(calc_match.group(4))
                i2_val = float(calc_match.group(5))
                ioc_val = float(calc_match.group(6))

                self.lbl_dcr_val.setText(f"{r_val:.3f} Ω")
                self.lbl_latch1.setText(f"{u1_val:.2f}V / {i1_val:.2f}A")
                self.lbl_latch2.setText(f"{u2_val:.2f}V / {i2_val:.2f}A")
                self.lbl_latch_ioc.setText(f"{ioc_val:.2f} A")
            except Exception:
                pass

        # 匹配保护/状态机状态转移
        state_match = STATE_PATTERN.search(line)
        if state_match:
            try:
                st_str = state_match.group(1)
                if st_str == "NORMAL":
                    self.lbl_protect_state.setText("🟢 正常运行 (NORMAL)")
                    self.lbl_protect_state.setStyleSheet("color: #10b981; font-weight: bold; font-size: 13px;")
                elif st_str == "TRIPPED":
                    self.lbl_protect_state.setText("🔴 越限跳闸 (TRIPPED)")
                    self.lbl_protect_state.setStyleSheet("color: #ef4444; font-weight: bold; font-size: 13px;")
                elif st_str == "RECOVERY_WAIT":
                    self.lbl_protect_state.setText("🟡 恢复等待 (RECOVERY_WAIT)")
                    self.lbl_protect_state.setStyleSheet("color: #f59e0b; font-weight: bold; font-size: 13px;")
                else:
                    self.lbl_protect_state.setText(f"🔵 计算状态 ({st_str})")
                    self.lbl_protect_state.setStyleSheet("color: #38bdf8; font-weight: bold; font-size: 13px;")
            except Exception:
                pass

    @Slot(bytes)
    def handle_raw_data(self, data: bytes):
        """如果勾选十六进制显示，在此将原始报文打印至终端"""
        if self.chk_hex_show.isChecked():
            hex_str = " ".join([f"{b:02X}" for b in data]) + " "
            self.append_log(hex_str, is_system=False, is_raw_hex=True)

    @Slot(str)
    def handle_serial_error(self, err_msg: str):
        self.status_bar.showMessage(err_msg)
        self.close_serial()

    def transmit_dac_setting(self):
        """通过串口向固件发送 DAC 电流控制命令
        格式: SetDAC:<电流值> (例如 SetDAC:4.50)
        """
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

    def update_of_en_ui_styles(self, state: int):
        """根据当前状态，动态更新 OF_EN 按钮样式和文本指示"""
        if state == 1:
            self.lbl_of_status.setText("🟢 已使能 (ON)")
            self.lbl_of_status.setStyleSheet("color: #10b981; font-weight: bold; font-family: Segoe UI; font-size: 13px;")
            self.btn_of_en_on.setStyleSheet(
                "background: qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:1, stop:0 #10b981, stop:1 #059669); "
                "border: 2px solid #34d399; font-weight: bold; color: white;"
            )
            self.btn_of_en_off.setStyleSheet(
                "background: #1e1e2f; border: 1px solid #334155; color: #64748b; font-weight: normal;"
            )
        elif state == 0:
            self.lbl_of_status.setText("🔴 已禁用 (OFF)")
            self.lbl_of_status.setStyleSheet("color: #ef4444; font-weight: bold; font-family: Segoe UI; font-size: 13px;")
            self.btn_of_en_on.setStyleSheet(
                "background: #1e1e2f; border: 1px solid #334155; color: #64748b; font-weight: normal;"
            )
            self.btn_of_en_off.setStyleSheet(
                "background: qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:1, stop:0 #ef4444, stop:1 #dc2626); "
                "border: 2px solid #f87171; font-weight: bold; color: white;"
            )
        else:
            self.lbl_of_status.setText("⚪ 离线状态 (OFFLINE)")
            self.lbl_of_status.setStyleSheet("color: #64748b; font-weight: bold; font-family: Segoe UI; font-size: 13px;")
            self.btn_of_en_on.setStyleSheet(
                "background: #1e1e2f; border: 1px solid #334155; color: #475569; font-weight: normal;"
            )
            self.btn_of_en_off.setStyleSheet(
                "background: #1e1e2f; border: 1px solid #334155; color: #475569; font-weight: normal;"
            )

    def transmit_of_en_setting(self, state: int):
        """通过串口发送 OF_EN 保护控制命令
        格式: SetOF:<0/1> (例如 SetOF:1)
        """
        if not self.ser or not self.ser.is_open:
            return

        cmd = f"SetOF:{state}\r\n"
        try:
            self.ser.write(cmd.encode('utf-8'))
            self.status_bar.showMessage(f"已下发 OF_EN 设定: {cmd.strip()}")
            self.append_log(f">>> 发送指令: {cmd.strip()}", is_system=True)
            self.update_of_en_ui_styles(state)
        except Exception as e:
            self.status_bar.showMessage(f"下发 OF_EN 设定失败: {str(e)}")

    def send_command(self):
        """发送终端输入的手动串口命令"""
        if not self.ser or not self.ser.is_open:
            return

        text = self.txt_send.text().strip()
        if not text:
            return

        try:
            cmd = text + "\r\n"
            self.ser.write(cmd.encode('utf-8'))
            self.txt_send.clear()
            self.append_log(f">>> 手动发码: {text}", is_system=True)
        except Exception as e:
            self.status_bar.showMessage(f"命令发送失败: {str(e)}")

    def append_log(self, text: str, is_system: bool = False, is_raw_hex: bool = False):
        """将信息追加至原始终端文本框"""
        cursor = self.txt_receive.textCursor()
        cursor.movePosition(QTextCursor.End)
        self.txt_receive.setTextCursor(cursor)

        if is_system:
            # 浅蓝系统日志
            self.txt_receive.insertHtml(f"<span style='color: #60a5fa;'><b>{text}</b></span><br/>")
        else:
            if is_raw_hex:
                self.txt_receive.insertPlainText(text)
            else:
                # 附带时间戳显示
                now = datetime.now().strftime("[%H:%M:%S.%f]")[:-3]
                self.txt_receive.insertPlainText(f"{now} | {text}\n")

        if self.chk_auto_scroll.isChecked():
            self.txt_receive.ensureCursorVisible()

    def clear_receive_area(self):
        self.txt_receive.clear()
        self.trend_plot.clear_data()

    def closeEvent(self, event):
        self.close_serial()
        event.accept()


if __name__ == "__main__":
    app = QApplication(sys.argv)
    app.setFont(QFont("Segoe UI", 10))
    window = PSAFirmwareConsole()
    window.show()
    sys.exit(app.exec())
