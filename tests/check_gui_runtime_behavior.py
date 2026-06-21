# -*- coding: utf-8 -*-
import os
import sys
from pathlib import Path

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

REPO_ROOT = Path(__file__).resolve().parents[1]
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))

from PySide6.QtWidgets import QApplication

from gui.main import PSAFirmwareConsole


class FakeSerial:
    def __init__(self):
        self.is_open = True
        self.writes = []

    def write(self, data):
        self.writes.append(data)

    def close(self):
        self.is_open = False


def assert_true(condition, label):
    if not condition:
        raise AssertionError(label)


def assert_equal(actual, expected, label):
    if actual != expected:
        raise AssertionError(f"{label}: expected {expected!r}, got {actual!r}")


def get_app():
    app = QApplication.instance()
    if app is None:
        app = QApplication([])
    return app


def check_layout_and_runtime_labels():
    get_app()
    window = PSAFirmwareConsole()
    main_layout = window.centralWidget().layout()

    # dac_group is optional (may not exist in all versions)
    if hasattr(window, "dac_group"):
        assert_true(main_layout.indexOf(window.dac_group) >= 0, "dac_group must be attached to the final layout if present")

    assert_true(hasattr(window, "system_group"), "GUI must expose the standalone system reset group")
    assert_true(main_layout.indexOf(window.system_group) >= 0, "system_group must be attached to the final layout")

    assert_true(hasattr(window, "diag_group"), "GUI must expose the calc/state diagnostics group")
    assert_true(main_layout.indexOf(window.diag_group) >= 0, "diag_group must be attached to the final layout")

    for attr_name in (
        "lbl_dcr_val",
        "lbl_latch1",
        "lbl_latch2",
        "lbl_latch_ioc",
        "lbl_protect_state",
        "btn_pause2a",
    ):
        assert_true(hasattr(window, attr_name), f"GUI is missing runtime status label: {attr_name}")


def check_calc_and_state_parsing():
    get_app()
    window = PSAFirmwareConsole()

    window.handle_line_received("[Calc] R:2.000R U1:400.00V I1:3.00A U2:398.00V I2:2.00A IOC:12.00A")
    assert_equal(window.lbl_dcr_val.text(), "2.000 Ω", "Calc resistance label")
    assert_equal(window.lbl_latch1.text(), "400.00V / 3.00A", "Calc latch1 label")
    assert_equal(window.lbl_latch2.text(), "398.00V / 2.00A", "Calc latch2 label")
    assert_equal(window.lbl_latch_ioc.text(), "12.00 A", "Calc IOC label shows unclamped applied current within 15A range")

    window.handle_line_received("[State] STATE:RECOVERY_WAIT")
    assert_true("RECOVERY_WAIT" in window.lbl_protect_state.text(), "State label must reflect RECOVERY_WAIT")


def check_legacy_of_en_ui_removed():
    """测试 GUI 已不再暴露旧版 OF_EN 专有控件与回执驱动逻辑"""
    get_app()
    window = PSAFirmwareConsole()

    for attr_name in (
        "update_of_en_ui_styles",
        "transmit_of_en_setting",
        "lbl_of_status",
        "btn_of_en_on",
        "btn_of_en_off",
    ):
        assert_true(not hasattr(window, attr_name), f"Legacy OF_EN UI contract must stay removed: {attr_name}")


def check_pause2a_button_behavior():
    """测试 2A 等待挂起/恢复按钮的行为"""
    get_app()
    window = PSAFirmwareConsole()
    fake_serial = FakeSerial()
    window.ser = fake_serial

    # 默认状态：按钮应该禁用
    assert_true(not window.btn_pause2a.isEnabled(), "btn_pause2a should be disabled by default")

    # 接收 WAIT_2A_STABLE_10S 状态，按钮应该启用且显示"挂起"
    window.handle_line_received("[State] STATE:WAIT_2A_STABLE_10S")
    assert_true(window.btn_pause2a.isEnabled(), "btn_pause2a should be enabled in WAIT_2A_STABLE_10S")
    assert_true("挂起" in window.btn_pause2a.text(), "btn_pause2a should show pause text")

    # 点击按钮应发送 DebugPause2A 命令
    window.btn_pause2a.click()
    assert_equal(fake_serial.writes[-1], b"DebugPause2A\r\n", "clicking paused btn sends DebugPause2A")

    # 模拟固件回执：先接收 PAUSE_FAILED（如果当前不在正确状态）
    # 然后接收 WAIT_2A_PAUSED 状态转换
    # 由于当前测试中没有实际的状态机状态跟踪，
    # 我们手动触发状态转换来测试按钮的"恢复"模式
    window.update_pause2a_button_state("WAIT_2A_PAUSED")
    assert_true(window.btn_pause2a.isEnabled(), "btn_pause2a should be enabled in WAIT_2A_PAUSED")
    assert_true("恢复" in window.btn_pause2a.text(), "btn_pause2a should show resume text")

    # 点击"恢复"按钮应发送 DebugResume2A 命令
    window.btn_pause2a.click()
    assert_equal(fake_serial.writes[-1], b"DebugResume2A\r\n", "clicking resumed btn sends DebugResume2A")

    # 接收其他状态，按钮应该禁用
    window.update_pause2a_button_state("MONITOR")
    assert_true(not window.btn_pause2a.isEnabled(), "btn_pause2a should be disabled in MONITOR")


def check_pause2a_button_resets_on_disconnect():
    """测试串口断开后 2A 挂起按钮必须复位，避免残留可点击状态"""
    get_app()
    window = PSAFirmwareConsole()
    fake_serial = FakeSerial()
    window.ser = fake_serial

    window.update_pause2a_button_state("WAIT_2A_PAUSED")
    assert_true(window.btn_pause2a.isEnabled(), "btn_pause2a should enter enabled resume mode before disconnect")
    assert_true("恢复" in window.btn_pause2a.text(), "btn_pause2a should show resume text before disconnect")

    window.close_serial()

    assert_true(not window.btn_pause2a.isEnabled(), "btn_pause2a should be disabled after disconnect")
    assert_true("挂起" in window.btn_pause2a.text(), "btn_pause2a should reset to pause text after disconnect")


def main():
    print("==================================================")
    print(" 开始执行 GUI 运行态布局与回执行为校验")
    print("==================================================")
    try:
        check_layout_and_runtime_labels()
        check_calc_and_state_parsing()
        check_legacy_of_en_ui_removed()
        check_pause2a_button_behavior()
        check_pause2a_button_resets_on_disconnect()
    except AssertionError as exc:
        print(f"测试失败: {exc}")
        print("==================================================")
        return 1

    print("==================================================")
    print(" 测试结论: GUI 运行态布局与回执行为校验通过!")
    print("==================================================")
    return 0


if __name__ == "__main__":
    sys.exit(main())
