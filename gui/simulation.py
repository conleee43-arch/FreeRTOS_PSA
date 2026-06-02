# -*- coding: utf-8 -*-
"""
PSA (电源适配器) 控制系统状态机演进与数据交互模拟程序
该程序为上位机图形展示/数据动画模拟，采用 PySide6 开发，
完全解耦了状态机模拟逻辑与 GUI 界面刷新。
"""

import sys
import math
import random
from datetime import datetime
from PySide6.QtCore import Qt, QTimer, QPointF, QRectF, Signal, QObject, Slot
from PySide6.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
    QGridLayout, QLabel, QPushButton, QDoubleSpinBox, QGroupBox, QFrame, QTextEdit
)
from PySide6.QtGui import QPainter, QPen, QColor, QBrush, QLinearGradient, QFont, QRadialGradient, QPainterPath

# ==============================================================================
# QSS 高保真扁平化深色视觉主题 (Premium Dark Theme)
# ==============================================================================
MODERN_STYLE = """
QMainWindow {
    background-color: #0b0b10;
}

QWidget {
    color: #cbd5e1;
    font-family: 'Segoe UI', -apple-system, BlinkMacSystemFont, Roboto, sans-serif;
    font-size: 13px;
}

QGroupBox {
    border: 1px solid #1e1e2f;
    border-radius: 10px;
    margin-top: 15px;
    font-weight: bold;
    color: #818cf8;
    background-color: #12121f;
}

QGroupBox::title {
    subcontrol-origin: margin;
    subcontrol-position: top left;
    left: 15px;
    padding: 0 8px;
}

QLabel {
    color: #94a3b8;
}

/* 核心参数数字显示框 */
QLabel#ValLabel {
    font-family: 'Consolas', 'Courier New', monospace;
    font-size: 24px;
    font-weight: bold;
    color: #38bdf8;
    background-color: #06060c;
    border: 1px solid #1e1e2f;
    border-radius: 6px;
    padding: 5px;
}

QLabel#HighlightValLabel {
    font-family: 'Consolas', 'Courier New', monospace;
    font-size: 32px;
    font-weight: bold;
    color: #10b981;
    background-color: #041a12;
    border: 2px solid #059669;
    border-radius: 8px;
    padding: 8px;
}

QDoubleSpinBox {
    background-color: #1e1e2f;
    border: 1px solid #334155;
    border-radius: 6px;
    padding: 5px;
    color: #f1f5f9;
    font-weight: bold;
    font-size: 14px;
}

QDoubleSpinBox:focus {
    border: 1px solid #6366f1;
}

/* 按钮样式 */
QPushButton {
    background: qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:1, stop:0 #4f46e5, stop:1 #4338ca);
    border: none;
    border-radius: 6px;
    color: white;
    padding: 10px 18px;
    font-weight: bold;
}

QPushButton:hover {
    background: qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:1, stop:0 #6366f1, stop:1 #4f46e5);
}

QPushButton:pressed {
    background: #3730a3;
}

QPushButton#faultBtn {
    background: qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:1, stop:0 #f43f5e, stop:1 #e11d48);
}

QPushButton#faultBtn:hover {
    background: qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:1, stop:0 #fda4af, stop:1 #f43f5e);
}

QPushButton#clearBtn {
    background: qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:1, stop:0 #0d9488, stop:1 #0f766e);
}

QPushButton#clearBtn:hover {
    background: qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:1, stop:0 #14b8a6, stop:1 #0d9488);
}

QTextEdit {
    background-color: #050508;
    border: 1px solid #1e1e2f;
    border-radius: 8px;
    padding: 8px;
    color: #f8fafc;
    font-family: 'Consolas', 'Courier New', monospace;
}
"""

# ==============================================================================
# 状态机节点定义
# ==============================================================================
class FlowNode:
    def __init__(self, key, label, x, y, w, h):
        self.key = key
        self.label = label
        self.x = x
        self.y = y
        self.w = w
        self.h = h


# ==============================================================================
# 流程图自定义绘制小部件 (Flowchart Widget)
# ==============================================================================
class FlowchartView(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setMinimumHeight(280)
        
        # 布局坐标与流程节点定义
        self.nodes = {
            "POWER_ON_TEST": FlowNode("POWER_ON_TEST", "上电测试\n(Power Test)", 40, 50, 110, 60),
            "DAC_3A_OUT": FlowNode("DAC_3A_OUT", "DAC 3A 输出\n(DAC 3A Out)", 200, 50, 110, 60),
            "STABLE_3A_CHECK": FlowNode("STABLE_3A_CHECK", "3A 稳定判定\n(3A Stable)", 360, 50, 110, 60),
            "DAC_2A_OUT": FlowNode("DAC_2A_OUT", "DAC 2A 输出\n(DAC 2A Out)", 520, 50, 110, 60),
            "STABLE_2A_CHECK": FlowNode("STABLE_2A_CHECK", "2A 稳定判定\n(2A Stable)", 680, 50, 110, 60),
            "CALC_RESISTANCE": FlowNode("CALC_RESISTANCE", "内阻计算配置\n(DCR Calc)", 840, 50, 110, 60),
            "MONITOR_MODE": FlowNode("MONITOR_MODE", "常态监控\n(Monitor)", 840, 170, 110, 60),
            "SHUTDOWN_STATE": FlowNode("SHUTDOWN_STATE", "系统关闭与监控\n(OF_EN=0, DAC=0)", 360, 170, 270, 60),
        }
        
        self.active_state = "POWER_ON_TEST"
        self.fault_active = False
        self.flow_offset = 0
        self.previous_state = ""
        
        # 动画定时器（用于流水灯粒子效果）
        self.anim_timer = QTimer(self)
        self.anim_timer.timeout.connect(self.update_animation)
        self.anim_timer.start(40)  # 25 fps

    def update_state(self, state, prev_state, fault_active):
        self.active_state = state
        self.previous_state = prev_state
        self.fault_active = fault_active
        self.update()

    def update_animation(self):
        self.flow_offset = (self.flow_offset + 1.5) % 40
        self.update()

    def draw_path_arrow(self, painter, points, active, is_red, label=""):
        """绘制多折线段及流动的指示粒子"""
        color = QColor(244, 63, 94) if is_red else (QColor(16, 185, 129) if active else QColor(51, 65, 85))
        pen = QPen(color, 2, Qt.SolidLine)
        painter.setPen(pen)
        
        # 绘制折线
        for i in range(len(points) - 1):
            painter.drawLine(points[i], points[i+1])
            
        # 终点画箭头
        p_last = points[-1]
        p_prev = points[-2]
        dx = p_last.x() - p_prev.x()
        dy = p_last.y() - p_prev.y()
        length = math.sqrt(dx*dx + dy*dy)
        if length > 0:
            ux = dx / length
            uy = dy / length
            arrow_len = 8
            ap1 = QPointF(p_last.x() - ux * arrow_len + uy * 4, p_last.y() - uy * arrow_len - ux * 4)
            ap2 = QPointF(p_last.x() - ux * arrow_len - uy * 4, p_last.y() - uy * arrow_len + ux * 4)
            painter.drawLine(p_last, ap1)
            painter.drawLine(p_last, ap2)
            
        # 计算路径总长度，并绘制沿着折线流动的小粒子
        segments = []
        total_len = 0.0
        for i in range(len(points) - 1):
            seg_dx = points[i+1].x() - points[i].x()
            seg_dy = points[i+1].y() - points[i].y()
            seg_len = math.sqrt(seg_dx*seg_dx + seg_dy*seg_dy)
            segments.append((points[i], points[i+1], seg_len))
            total_len += seg_len
            
        if active and total_len > 0:
            dot_color = QColor(251, 113, 133) if is_red else QColor(52, 211, 153)
            painter.setBrush(QBrush(dot_color))
            painter.setPen(Qt.NoPen)
            num_dots = 4
            for d in range(num_dots):
                dist = (((self.flow_offset / 40.0) + (d / num_dots)) % 1.0) * total_len
                curr_dist = 0.0
                for p_start, p_end, s_len in segments:
                    if curr_dist <= dist <= curr_dist + s_len:
                        t = (dist - curr_dist) / s_len
                        x = p_start.x() + t * (p_end.x() - p_start.x())
                        y = p_start.y() + t * (p_end.y() - p_start.y())
                        painter.drawEllipse(QPointF(x, y), 4, 4)
                        break
                    curr_dist += s_len
                    
        # 绘制文本指示
        if label:
            painter.setFont(QFont("Segoe UI", 9))
            painter.setPen(QPen(QColor(148, 163, 184)))
            mx = (points[0].x() + points[1].x()) / 2
            my = (points[0].y() + points[1].y()) / 2
            painter.drawText(int(mx - 15), int(my - 5), label)

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.Antialiasing)
        
        # 背景填充
        painter.fillRect(self.rect(), QColor(10, 10, 15))
        
        # 1. 定义连线几何数据与状态联动关系
        paths = [
            # 主测量流程连线
            ("POWER_ON_TEST", "DAC_3A_OUT", 
             [QPointF(150, 80), QPointF(200, 80)], 
             self.active_state == "DAC_3A_OUT" and self.previous_state == "POWER_ON_TEST", False, "正常"),
             
            ("DAC_3A_OUT", "STABLE_3A_CHECK", 
             [QPointF(310, 80), QPointF(360, 80)], 
             self.active_state == "STABLE_3A_CHECK", False, ""),
             
            ("STABLE_3A_CHECK", "DAC_2A_OUT", 
             [QPointF(470, 80), QPointF(520, 80)], 
             self.active_state == "DAC_2A_OUT", False, ""),
             
            ("DAC_2A_OUT", "STABLE_2A_CHECK", 
             [QPointF(630, 80), QPointF(680, 80)], 
             self.active_state == "STABLE_2A_CHECK", False, ""),
             
            ("STABLE_2A_CHECK", "CALC_RESISTANCE", 
             [QPointF(790, 80), QPointF(840, 80)], 
             self.active_state == "CALC_RESISTANCE", False, ""),
             
            ("CALC_RESISTANCE", "MONITOR_MODE", 
             [QPointF(895, 110), QPointF(895, 170)], 
             self.active_state == "MONITOR_MODE", False, ""),
             
            # 异常与关闭流转连线
            ("POWER_ON_TEST", "SHUTDOWN_STATE", 
             [QPointF(95, 110), QPointF(95, 200), QPointF(360, 200)], 
             self.active_state == "SHUTDOWN_STATE" and self.previous_state == "POWER_ON_TEST", True, "否"),
             
            ("SHUTDOWN_STATE", "POWER_ON_TEST", 
             [QPointF(495, 230), QPointF(495, 250), QPointF(95, 250), QPointF(95, 110)], 
             self.active_state == "POWER_ON_TEST" and self.previous_state == "SHUTDOWN_STATE", False, "恢复")
        ]
        
        # 绘制所有物理连线与流动粒子
        for src, dst, pts, active, is_red, label in paths:
            self.draw_path_arrow(painter, pts, active, is_red, label)
            
        # 2. 绘制各个节点框
        for key, node in self.nodes.items():
            rect = QRectF(node.x, node.y, node.w, node.h)
            is_active = (self.active_state == key)
            is_shutdown_node = (key == "SHUTDOWN_STATE")
            
            if is_active:
                if is_shutdown_node or self.fault_active:
                    border_color = QColor(244, 63, 94) # 亮红色警告
                    bg_color = QColor(225, 29, 72, 40)
                    text_color = QColor(255, 255, 255)
                    # 放射状霓虹边缘发光效果
                    glow = QRadialGradient(rect.center(), rect.width(), rect.center())
                    glow.setColorAt(0.0, QColor(244, 63, 94, 80))
                    glow.setColorAt(1.0, QColor(244, 63, 94, 0))
                    painter.fillRect(rect.adjusted(-10, -10, 10, 10), QBrush(glow))
                else:
                    border_color = QColor(16, 185, 129) # 亮绿色激活
                    bg_color = QColor(5, 150, 105, 40)
                    text_color = QColor(255, 255, 255)
                    # 放射状霓虹发光
                    glow = QRadialGradient(rect.center(), rect.width(), rect.center())
                    glow.setColorAt(0.0, QColor(16, 185, 129, 80))
                    glow.setColorAt(1.0, QColor(16, 185, 129, 0))
                    painter.fillRect(rect.adjusted(-10, -10, 10, 10), QBrush(glow))
            else:
                # 未被执行或失效状态
                border_color = QColor(51, 65, 85)
                bg_color = QColor(30, 41, 59, 20)
                text_color = QColor(148, 163, 184)
                
            # 绘制圆角卡片矩形
            painter.setBrush(QBrush(bg_color))
            pen = QPen(border_color, 2.5 if is_active else 1.2, Qt.SolidLine)
            painter.setPen(pen)
            painter.drawRoundedRect(rect, 8, 8)
            
            # 渲染文字
            painter.setPen(QPen(text_color))
            painter.setFont(QFont("Segoe UI", 9, QFont.Bold if is_active else QFont.Normal))
            painter.drawText(rect, Qt.AlignCenter, node.label)


# ==============================================================================
# 固件状态机与物理回路数据仿真模型 (Model)
# ==============================================================================
class PSAControlSimulator(QObject):
    # 状态与物理信号定义，用于解耦数据交互
    state_changed = Signal(str, str, bool) # current_state, prev_state, fault_active
    telemetry_updated = Signal(dict)       # 包含 U1, I1, U2, I2, r, V_out, I_out, of_en 等数据
    log_triggered = Signal(str, str)       # message, level

    def __init__(self):
        super().__init__()
        # 物理常量参数
        self.u0 = 394.0                   # 供电基准母线开路电压 (V)
        self.r_real = 2.05                 # 模拟系统真实等效内阻 (Ω)
        
        # 运行环境交互状态
        self.is_normal = True              # 故障标志位
        self.of_en = 0                     # OF_EN 状态 (0/1)
        self.dac_val = 0.0                 # DAC 模拟设定输出量 (A)
        
        # 3A/2A 设定变化量 (由 GUI 配置下发)
        self.set_change_3a = 3.0           # 3A 阶跃设定值 (A)
        self.set_change_2a = -1.0          # 2A 阶跃降落设定值 (A)
        
        # 实际阶跃过程物理变化值
        self.actual_change_3a = 0.0
        self.actual_change_2a = 0.0
        
        # 遥测锁存物理量
        self.u1, self.i1 = 0.0, 0.0
        self.u2, self.i2 = 0.0, 0.0
        self.calculated_r = 0.0
        
        # 物理输出实时值
        self.current_i = 0.0
        self.current_v = 0.0
        
        # 状态机管理
        self.state = "POWER_ON_TEST"
        self.prev_state = ""
        self.ticks_in_state = 0
        
        # 仿真时间控制 (50ms 仿真步长)
        self.sim_timer = QTimer(self)
        self.sim_timer.timeout.connect(self.sim_step)

    def start(self):
        self.sim_timer.start(50)
        self.log("仿真引擎启动", "INFO")

    def stop(self):
        self.sim_timer.stop()

    def reset_system(self):
        """主测量复位起点动作"""
        self.prev_state = self.state
        self.state = "POWER_ON_TEST"
        self.of_en = 1
        self.dac_val = 0.0
        self.actual_change_3a = 0.0
        self.actual_change_2a = 0.0
        self.u1, self.i1 = 0.0, 0.0
        self.u2, self.i2 = 0.0, 0.0
        self.calculated_r = 0.0
        self.current_i = 0.0
        self.current_v = self.u0
        self.ticks_in_state = 0
        self.is_normal = True
        self.state_changed.emit(self.state, self.prev_state, False)
        self.log("系统接收到重置命令，重新进入 [上电测试]", "INFO")

    def inject_fault(self):
        """注入物理故障异常"""
        self.is_normal = False
        self.prev_state = self.state
        self.state = "SHUTDOWN_STATE"
        self.of_en = 0
        self.dac_val = 0.0
        self.ticks_in_state = 0
        self.state_changed.emit(self.state, self.prev_state, True)
        self.log("[ALERT] 注入物理故障 (过温/过压)，下位机紧急熔断: 拉低 OF_EN=0, DAC=0", "ERROR")

    def clear_fault(self):
        """恢复物理故障并自愈重新测量"""
        if not self.is_normal:
            self.is_normal = True
            self.log("物理故障清除，执行环境恢复，系统重新导流至主测量起点", "INFO")
            self.reset_system()

    def log(self, msg, level="DEBUG"):
        self.log_triggered.emit(msg, level)

    def sim_step(self):
        """
        物理仿真计算与状态机演进循环 (运行于下位机硬实时步长)
        """
        self.ticks_in_state += 1
        noise_i = (random.random() - 0.5) * 0.005  # 微波噪点
        noise_v = (random.random() - 0.5) * 0.08
        
        # 1. 系统被强制关闭状态监控
        if self.state == "SHUTDOWN_STATE":
            self.of_en = 0
            self.dac_val = 0.0
            # 物理大电容余电快速释放衰减物理模拟
            self.current_i += 0.25 * (0.0 - self.current_i)
            self.current_v += 0.25 * (0.0 - self.current_v)
            
        # 2. 上电自检测试状态
        elif self.state == "POWER_ON_TEST":
            self.of_en = 1
            self.dac_val = 0.0
            self.current_i = 0.0
            self.current_v = self.u0 + noise_v
            
            # 自检持续 1.2 秒 (24 个 ticks)
            if self.ticks_in_state >= 24:
                if not self.is_normal:
                    self.prev_state = self.state
                    self.state = "SHUTDOWN_STATE"
                    self.ticks_in_state = 0
                    self.state_changed.emit(self.state, self.prev_state, True)
                    self.log("[自检不通过] 系统环境存在异常，进入 [关闭系统] 持续监控", "ERROR")
                else:
                    self.prev_state = self.state
                    self.state = "DAC_3A_OUT"
                    self.ticks_in_state = 0
                    self.state_changed.emit(self.state, self.prev_state, False)
                    self.log("[自检通过] 系统一切正常，跳转至 [DAC 3A 输出]", "SUCCESS")

        # 3. DAC 3A 输出状态
        elif self.state == "DAC_3A_OUT":
            self.dac_val = 3.0
            # 硬件配置下发阶段延时 0.4 秒 (8 ticks)
            if self.ticks_in_state >= 8:
                self.prev_state = self.state
                self.state = "STABLE_3A_CHECK"
                self.ticks_in_state = 0
                self.state_changed.emit(self.state, self.prev_state, False)
                self.log(f"DAC 设定下发完毕，进入 [3A 稳定判定]，目标阶跃电流变化量: {self.set_change_3a:.2f}A", "INFO")

        # 4. 3A 稳定判定状态 (带有物理指数级上升模拟)
        elif self.state == "STABLE_3A_CHECK":
            # 一阶低通滤波模拟大电感阻抗的物理变化爬坡过程 (阻尼系数 0.06 较慢)
            target_curr = self.set_change_3a
            self.actual_change_3a += 0.06 * (target_curr - self.actual_change_3a)
            self.current_i = self.actual_change_3a + noise_i
            
            # 物理补偿公式：随着负载电流上升，上位机补偿线损，因此电压上升
            self.current_v = (self.u0 - 3.0 * self.r_real) + self.current_i * self.r_real + noise_v
            
            # 物理稳定条件判定：实际值逼近设定值 0.01A 误差内，且防抖持续 5 ticks (250ms)
            if abs(self.actual_change_3a - self.set_change_3a) < 0.01:
                # 固件锁存 U1, I1
                self.u1 = round(self.current_v, 2)
                self.i1 = round(self.current_i, 2)
                self.prev_state = self.state
                self.state = "DAC_2A_OUT"
                self.ticks_in_state = 0
                self.state_changed.emit(self.state, self.prev_state, False)
                self.log(f"[物理稳态锁定] 3A阶跃电流彻底稳定！固件已锁存 U1: {self.u1:.2f}V, I1: {self.i1:.2f}A", "SUCCESS")

        # 5. DAC 2A 输出状态
        elif self.state == "DAC_2A_OUT":
            self.dac_val = 2.0
            # 硬件配置下发阶段延时 0.4 秒，并向 GUI 上报第一组锁存值
            if self.ticks_in_state >= 8:
                self.prev_state = self.state
                self.state = "STABLE_2A_CHECK"
                self.ticks_in_state = 0
                self.state_changed.emit(self.state, self.prev_state, False)
                self.log(f"下发 2A 阶跃设定，进入 [2A 稳定判定]，目标阶跃降落变化量: {self.set_change_2a:.2f}A", "INFO")

        # 6. 2A 稳定判定状态 (带有快速回落稳定模拟)
        elif self.state == "STABLE_2A_CHECK":
            # 模拟向下阶跃电平释放响应快于上升过程 (阻尼系数 0.18 明显快于 3A 阶跃的 0.06)
            target_curr = self.set_change_3a + self.set_change_2a # 3A - 1A = 2A
            self.actual_change_2a += 0.18 * (self.set_change_2a - self.actual_change_2a)
            self.current_i = (self.set_change_3a + self.actual_change_2a) + noise_i
            self.current_v = (self.u0 - 3.0 * self.r_real) + self.current_i * self.r_real + noise_v
            
            if abs(self.actual_change_2a - self.set_change_2a) < 0.01:
                # 固件锁存 U2, I2
                self.u2 = round(self.current_v, 2)
                self.i2 = round(self.current_i, 2)
                self.prev_state = self.state
                self.state = "CALC_RESISTANCE"
                self.ticks_in_state = 0
                self.state_changed.emit(self.state, self.prev_state, False)
                self.log(f"[物理稳态锁定] 2A降落阶跃已稳定！固件已锁存 U2: {self.u2:.2f}V, I2: {self.i2:.2f}A", "SUCCESS")

        # 7. 内阻求解与电流限制配置状态
        elif self.state == "CALC_RESISTANCE":
            # 核心计算逻辑 r = (U1 - U2) / (I1 - I2)
            denom = self.i1 - self.i2
            if abs(denom) > 0.01:
                self.calculated_r = abs(self.u1 - self.u2) / denom
            else:
                self.calculated_r = 0.0
                
            if self.ticks_in_state >= 20: # 留给 GUI 动画渲染与数值高亮 1.0s
                self.prev_state = self.state
                self.state = "MONITOR_MODE"
                self.ticks_in_state = 0
                self.state_changed.emit(self.state, self.prev_state, False)
                self.log(f"内阻解算成功: {self.calculated_r:.3f} Ω。固件已更新安全限制电流 IOC 至最大值 12A，进入 [常态监控模式]", "SUCCESS")

        # 8. 常态监控模式状态
        elif self.state == "MONITOR_MODE":
            # 持续稳定输出，注入正常低通微波杂散
            self.current_i = 2.0 + noise_i
            self.current_v = (self.u0 - 3.0 * self.r_real) + self.current_i * self.r_real + noise_v

        # 数据流分发组装，向上位机 GUI 通信
        telemetry = {
            "state": self.state,
            "of_en": self.of_en,
            "dac_val": self.dac_val,
            "current_i": self.current_i,
            "current_v": self.current_v,
            "set_change_3a": self.set_change_3a,
            "actual_change_3a": self.actual_change_3a,
            "set_change_2a": self.set_change_2a,
            "actual_change_2a": self.actual_change_2a,
            "u1": self.u1,
            "i1": self.i1,
            "u2": self.u2,
            "i2": self.i2,
            "r": self.calculated_r
        }
        self.telemetry_updated.emit(telemetry)


# ==============================================================================
# 上位机图形显示控制主界面 (View / Controller)
# ==============================================================================
class PSASimulationGUI(QMainWindow):
    def __init__(self, simulator: PSAControlSimulator):
        super().__init__()
        self.sim = simulator
        
        self.init_ui()
        self.connect_signals()

    def init_ui(self):
        self.setWindowTitle("PSA 适配器物理回路状态演进上位机仿真系统 (基于双向自适应阶跃)")
        self.resize(1100, 780)
        self.setStyleSheet(MODERN_STYLE)
        
        central_widget = QWidget(self)
        self.setCentralWidget(central_widget)
        
        # 整体布局结构
        main_layout = QVBoxLayout(central_widget)
        main_layout.setContentsMargins(15, 15, 15, 15)
        main_layout.setSpacing(15)
        
        # 1. 顶部自定义高亮流程图区域
        self.flowchart = FlowchartView(self)
        main_layout.addWidget(self.flowchart)
        
        # 2. 中部数据展示与控制的左右分栏
        middle_layout = QHBoxLayout()
        middle_layout.setSpacing(15)
        
        # 2.1 左栏：数据对比与参数看板
        left_layout = QVBoxLayout()
        left_layout.setSpacing(15)
        
        # 2.1.1 设定与实际变化量对比区
        contrast_group = QGroupBox("自适应阶跃负荷数据对比区")
        contrast_grid = QGridLayout(contrast_group)
        contrast_grid.setContentsMargins(15, 20, 15, 15)
        contrast_grid.setSpacing(12)
        
        # 3A 阶跃控制组
        contrast_grid.addWidget(QLabel("3A 设定阶跃电流变化量 (A):"), 0, 0)
        self.spin_set_3a = QDoubleSpinBox()
        self.spin_set_3a.setRange(1.0, 5.0)
        self.spin_set_3a.setSingleStep(0.1)
        self.spin_set_3a.setValue(3.0)
        self.spin_set_3a.valueChanged.connect(self.update_simulator_settings)
        contrast_grid.addWidget(self.spin_set_3a, 0, 1)
        
        contrast_grid.addWidget(QLabel("3A 当前实际变化量 (A):"), 0, 2)
        self.lbl_actual_3a = QLabel("0.00 A")
        self.lbl_actual_3a.setObjectName("ValLabel")
        self.lbl_actual_3a.setAlignment(Qt.AlignCenter)
        contrast_grid.addWidget(self.lbl_actual_3a, 0, 3)
        
        # 2A 阶跃控制组
        contrast_grid.addWidget(QLabel("2A 阶跃降落设定变化量 (A):"), 1, 0)
        self.spin_set_2a = QDoubleSpinBox()
        self.spin_set_2a.setRange(-3.0, -0.1)
        self.spin_set_2a.setSingleStep(0.1)
        self.spin_set_2a.setValue(-1.0)
        self.spin_set_2a.valueChanged.connect(self.update_simulator_settings)
        contrast_grid.addWidget(self.spin_set_2a, 1, 1)
        
        contrast_grid.addWidget(QLabel("2A 当前实际变化量 (A):"), 1, 2)
        self.lbl_actual_2a = QLabel("0.00 A")
        self.lbl_actual_2a.setObjectName("ValLabel")
        self.lbl_actual_2a.setAlignment(Qt.AlignCenter)
        contrast_grid.addWidget(self.lbl_actual_2a, 1, 3)
        
        left_layout.addWidget(contrast_group)
        
        # 2.1.2 核心参数锁存监控面板
        latch_group = QGroupBox("内阻解算核心遥测参数舱")
        latch_grid = QGridLayout(latch_group)
        latch_grid.setContentsMargins(15, 20, 15, 15)
        latch_grid.setSpacing(12)
        
        # U1/I1 锁存指示
        latch_grid.addWidget(QLabel("3A 锁存电压 (U1):"), 0, 0)
        self.lbl_u1 = QLabel("---- V")
        self.lbl_u1.setObjectName("ValLabel")
        self.lbl_u1.setAlignment(Qt.AlignCenter)
        latch_grid.addWidget(self.lbl_u1, 0, 1)
        
        latch_grid.addWidget(QLabel("3A 锁存电流 (I1):"), 0, 2)
        self.lbl_i1 = QLabel("---- A")
        self.lbl_i1.setObjectName("ValLabel")
        self.lbl_i1.setAlignment(Qt.AlignCenter)
        latch_grid.addWidget(self.lbl_i1, 0, 3)
        
        # U2/I2 锁存指示
        latch_grid.addWidget(QLabel("2A 锁存电压 (U2):"), 1, 0)
        self.lbl_u2 = QLabel("---- V")
        self.lbl_u2.setObjectName("ValLabel")
        self.lbl_u2.setAlignment(Qt.AlignCenter)
        latch_grid.addWidget(self.lbl_u2, 1, 1)
        
        latch_grid.addWidget(QLabel("2A 锁存电流 (I2):"), 1, 2)
        self.lbl_i2 = QLabel("---- A")
        self.lbl_i2.setObjectName("ValLabel")
        self.lbl_i2.setAlignment(Qt.AlignCenter)
        latch_grid.addWidget(self.lbl_i2, 1, 3)
        
        # 内阻求解最终结果 (高亮放大显示)
        r_label = QLabel("求解等效内阻 (r):")
        r_label.setFont(QFont("Segoe UI", 11, QFont.Bold))
        latch_grid.addWidget(r_label, 2, 0, 1, 2)
        
        self.lbl_r = QLabel("---- Ω")
        self.lbl_r.setObjectName("HighlightValLabel")
        self.lbl_r.setAlignment(Qt.AlignCenter)
        latch_grid.addWidget(self.lbl_r, 2, 2, 1, 2)
        
        left_layout.addWidget(latch_group)
        middle_layout.addLayout(left_layout, stretch=3)
        
        # 2.2 右栏：仿真控制中心与日志记录器
        right_layout = QVBoxLayout()
        right_layout.setSpacing(15)
        
        # 2.2.1 物理遥测实时状态显示
        phys_group = QGroupBox("实时测量指示器 (下位机采集上报)")
        phys_grid = QGridLayout(phys_group)
        phys_grid.setContentsMargins(15, 20, 15, 15)
        phys_grid.setSpacing(10)
        
        phys_grid.addWidget(QLabel("当前实际电压 (Vo_out):"), 0, 0)
        self.lbl_curr_v = QLabel("0.00 V")
        self.lbl_curr_v.setObjectName("ValLabel")
        self.lbl_curr_v.setStyleSheet("color: #fb923c;") # 高压危险警告色
        self.lbl_curr_v.setAlignment(Qt.AlignCenter)
        phys_grid.addWidget(self.lbl_curr_v, 0, 1)
        
        phys_grid.addWidget(QLabel("当前实际电流 (Co_out):"), 1, 0)
        self.lbl_curr_i = QLabel("0.00 A")
        self.lbl_curr_i.setObjectName("ValLabel")
        self.lbl_curr_i.setStyleSheet("color: #34d399;") # 正常工作色
        self.lbl_curr_i.setAlignment(Qt.AlignCenter)
        phys_grid.addWidget(self.lbl_curr_i, 1, 1)
        
        phys_grid.addWidget(QLabel("OF_EN 引脚物理状态:"), 2, 0)
        self.lbl_of_en = QLabel("🔴 关闭")
        self.lbl_of_en.setFont(QFont("Segoe UI", 11, QFont.Bold))
        self.lbl_of_en.setAlignment(Qt.AlignCenter)
        phys_grid.addWidget(self.lbl_of_en, 2, 1)
        
        right_layout.addWidget(phys_group)
        
        # 2.2.2 仿真系统调试动作交互区
        control_group = QGroupBox("仿真回路交互操作中心")
        control_box = QVBoxLayout(control_group)
        control_box.setContentsMargins(15, 20, 15, 15)
        control_box.setSpacing(12)
        
        self.btn_reset = QPushButton("启动系统 / 重新测量 (Reset)")
        control_box.addWidget(self.btn_reset)
        
        self.btn_inject_fault = QPushButton("注入物理故障 (过温 / 过压)")
        self.btn_inject_fault.setObjectName("faultBtn")
        control_box.addWidget(self.btn_inject_fault)
        
        self.btn_clear_fault = QPushButton("清除物理故障 (自愈动作)")
        self.btn_clear_fault.setObjectName("clearBtn")
        control_box.addWidget(self.btn_clear_fault)
        
        right_layout.addWidget(control_group)
        middle_layout.addLayout(right_layout, stretch=2)
        
        main_layout.addLayout(middle_layout)
        
        # 3. 底部调试与通信监视窗口
        log_group = QGroupBox("下位机系统通信与仿真日志终端监视舱")
        log_layout = QVBoxLayout(log_group)
        log_layout.setContentsMargins(15, 20, 15, 15)
        
        self.txt_log = QTextEdit()
        self.txt_log.setReadOnly(True)
        self.txt_log.setMinimumHeight(120)
        log_layout.addWidget(self.txt_log)
        
        main_layout.addWidget(log_group)

    def connect_signals(self):
        """绑定信号传输逻辑，使模拟状态演进驱动界面更新"""
        self.sim.state_changed.connect(self.handle_state_changed)
        self.sim.telemetry_updated.connect(self.handle_telemetry_updated)
        self.sim.log_triggered.connect(self.handle_log_triggered)
        
        # 绑定按钮交互
        self.btn_reset.clicked.connect(self.sim.reset_system)
        self.btn_inject_fault.clicked.connect(self.sim.inject_fault)
        self.btn_clear_fault.clicked.connect(self.sim.clear_fault)

    def update_simulator_settings(self):
        """同步 GUI 阶跃设定参数到下位机仿真器"""
        self.sim.set_change_3a = self.spin_set_3a.value()
        self.sim.set_change_2a = self.spin_set_2a.value()

    @Slot(str, str, bool)
    def handle_state_changed(self, current_state, prev_state, fault_active):
        """更新图形化流程图高亮状态"""
        self.flowchart.update_state(current_state, prev_state, fault_active)

    @Slot(dict)
    def handle_telemetry_updated(self, telemetry):
        """物理测量信号刷新，高保真动态显示数据"""
        # 更新实际变化量
        self.lbl_actual_3a.setText(f"{telemetry['actual_change_3a']:.2f} A")
        self.lbl_actual_2a.setText(f"{telemetry['actual_change_2a']:.2f} A")
        
        # 更新核心锁存指标
        if telemetry['u1'] > 0:
            self.lbl_u1.setText(f"{telemetry['u1']:.2f} V")
            self.lbl_i1.setText(f"{telemetry['i1']:.2f} A")
        else:
            self.lbl_u1.setText("---- V")
            self.lbl_i1.setText("---- A")
            
        if telemetry['u2'] > 0:
            self.lbl_u2.setText(f"{telemetry['u2']:.2f} V")
            self.lbl_i2.setText(f"{telemetry['i2']:.2f} A")
        else:
            self.lbl_u2.setText("---- V")
            self.lbl_i2.setText("---- A")
            
        # 最终内阻更新与突出展示效果
        if telemetry['r'] > 0:
            self.lbl_r.setText(f"{telemetry['r']:.3f} Ω")
        else:
            self.lbl_r.setText("---- Ω")
            
        # 实时测量指示刷新
        self.lbl_curr_v.setText(f"{telemetry['current_v']:.2f} V")
        self.lbl_curr_i.setText(f"{telemetry['current_i']:.2f} A")
        
        # 状态引脚指示
        if telemetry['of_en'] == 1:
            self.lbl_of_en.setText("🟢 正常使能 (ON)")
            self.lbl_of_en.setStyleSheet("color: #10b981; font-weight: bold;")
        else:
            self.lbl_of_en.setText("🔴 安全禁用 (OFF)")
            self.lbl_of_en.setStyleSheet("color: #f43f5e; font-weight: bold;")

    @Slot(str, str)
    def handle_log_triggered(self, msg, level):
        """将仿真底层通信格式化打印至监控舱日志区"""
        now = datetime.now().strftime("[%H:%M:%S.%f]")[:-3]
        
        # 根据日志级别渲染不同的高亮 HTML
        if level == "ERROR":
            html_text = f"<span style='color: #f43f5e;'>{now} | <b>{msg}</b></span><br/>"
        elif level == "SUCCESS":
            html_text = f"<span style='color: #10b981;'>{now} | <b>{msg}</b></span><br/>"
        elif level == "INFO":
            html_text = f"<span style='color: #60a5fa;'>{now} | {msg}</span><br/>"
        else:
            html_text = f"<span style='color: #94a3b8;'>{now} | {msg}</span><br/>"
            
        cursor = self.txt_log.textCursor()
        cursor.movePosition(self.txt_log.textCursor().End)
        self.txt_log.setTextCursor(cursor)
        self.txt_log.insertHtml(html_text)
        self.txt_log.ensureCursorVisible()


# ==============================================================================
# 系统入口程序
# ==============================================================================
if __name__ == "__main__":
    app = QApplication(sys.argv)
    app.setFont(QFont("Segoe UI", 10))
    
    # 实例化 Model 层与 View/Controller 层
    simulator = PSAControlSimulator()
    gui = PSASimulationGUI(simulator)
    
    # 建立物理连接，启动定时仿真驱动
    simulator.start()
    
    # 初始触发进入自检流程
    simulator.reset_system()
    
    gui.show()
    sys.exit(app.exec())
