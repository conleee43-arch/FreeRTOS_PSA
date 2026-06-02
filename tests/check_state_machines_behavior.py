# -*- coding: utf-8 -*-
from enum import IntEnum
import sys


class ProtectionState(IntEnum):
    NORMAL = 0
    TRIPPED = 1
    RECOVERY_WAIT = 2


class CalcState(IntEnum):
    WAIT_SAFE = 0
    SET_3A = 1
    WAIT_3A_STABLE = 2
    LATCH_3A = 3
    SET_2A = 4
    WAIT_2A_STABLE = 5
    LATCH_2A = 6
    CALC_RESISTANCE = 7
    MONITOR = 8


OTP_TRIP_THRESHOLD = 85.0
OTP_RECOVERY_THRESHOLD = 75.0
OVP_TRIP_THRESHOLD = 420.0
OVP_RECOVERY_THRESHOLD = 390.0
OCP_TRIP_THRESHOLD = 12.0
OCP_RECOVERY_THRESHOLD = 10.0
RECOVERY_OBSERVE_MS = 2000
STEP_3A_STABLE_MS = 100
STEP_2A_STABLE_MS = 100


class OutputProtectionModel:
    def __init__(self):
        self.state = ProtectionState.TRIPPED
        self.recovery_start_tick = 0

    def update(self, temperature_c, vo_out_v, co_out_a, tick_ms):
        output = {
            "disable_output": 0,
            "enable_output": 0,
            "reset_calc_control": 0,
        }
        fault_active = (
            temperature_c >= OTP_TRIP_THRESHOLD
            or vo_out_v >= OVP_TRIP_THRESHOLD
            or co_out_a >= OCP_TRIP_THRESHOLD
        )
        safe_limit = (
            temperature_c < OTP_RECOVERY_THRESHOLD
            and vo_out_v < OVP_RECOVERY_THRESHOLD
            and co_out_a < OCP_RECOVERY_THRESHOLD
        )

        if self.state == ProtectionState.NORMAL:
            if fault_active:
                self.state = ProtectionState.TRIPPED
                output["disable_output"] = 1
                output["reset_calc_control"] = 1
        elif self.state == ProtectionState.TRIPPED:
            if safe_limit:
                self.state = ProtectionState.RECOVERY_WAIT
                self.recovery_start_tick = tick_ms
            else:
                output["disable_output"] = 1
                output["reset_calc_control"] = 1
        elif self.state == ProtectionState.RECOVERY_WAIT:
            if fault_active or not safe_limit:
                self.state = ProtectionState.TRIPPED
                output["disable_output"] = 1
                output["reset_calc_control"] = 1
            elif tick_ms - self.recovery_start_tick >= RECOVERY_OBSERVE_MS:
                self.state = ProtectionState.NORMAL
                output["enable_output"] = 1
        else:
            self.state = ProtectionState.NORMAL

        return output


class CalcControlModel:
    def __init__(self):
        self.state = CalcState.WAIT_SAFE
        self.state_start_tick = 0
        self.u1 = 0.0
        self.i1 = 0.0
        self.u2 = 0.0
        self.i2 = 0.0
        self.resistance = 0.0

    def update(self, vo_out_v, co_out_a, tick_ms, safe_allowed, reset_request=False):
        output = {
            "set_current_a": 0.0,
            "change_current": 0,
            "publish_calc_report": 0,
            "enter_monitor": 0,
            "calculated_resistance": self.resistance,
            "u1": self.u1,
            "i1": self.i1,
            "u2": self.u2,
            "i2": self.i2,
        }

        if reset_request or not safe_allowed:
            self.state = CalcState.WAIT_SAFE
            self.state_start_tick = 0
            self.u1 = 0.0
            self.i1 = 0.0
            self.u2 = 0.0
            self.i2 = 0.0
            self.resistance = 0.0
            output["set_current_a"] = 0.0
            output["change_current"] = 1
            output["calculated_resistance"] = 0.0
            return output

        keep_running = True
        while keep_running:
            keep_running = False

            if self.state == CalcState.WAIT_SAFE:
                if safe_allowed:
                    self.state = CalcState.SET_3A
                    keep_running = True
            elif self.state == CalcState.SET_3A:
                output["set_current_a"] = 3.0
                output["change_current"] = 1
                self.state_start_tick = tick_ms
                self.state = CalcState.WAIT_3A_STABLE
            elif self.state == CalcState.WAIT_3A_STABLE:
                if tick_ms - self.state_start_tick >= STEP_3A_STABLE_MS:
                    self.state = CalcState.LATCH_3A
                    keep_running = True
            elif self.state == CalcState.LATCH_3A:
                self.u1 = vo_out_v
                self.i1 = co_out_a
                output["u1"] = self.u1
                output["i1"] = self.i1
                self.state = CalcState.SET_2A
                keep_running = True
            elif self.state == CalcState.SET_2A:
                output["set_current_a"] = 2.0
                output["change_current"] = 1
                self.state_start_tick = tick_ms
                self.state = CalcState.WAIT_2A_STABLE
            elif self.state == CalcState.WAIT_2A_STABLE:
                if tick_ms - self.state_start_tick >= STEP_2A_STABLE_MS:
                    self.state = CalcState.LATCH_2A
                    keep_running = True
            elif self.state == CalcState.LATCH_2A:
                self.u2 = vo_out_v
                self.i2 = co_out_a
                output["u2"] = self.u2
                output["i2"] = self.i2
                self.state = CalcState.CALC_RESISTANCE
                keep_running = True
            elif self.state == CalcState.CALC_RESISTANCE:
                di = self.i1 - self.i2
                if di > 0.01:
                    resistance_candidate = (self.u1 - self.u2) / di
                    if resistance_candidate >= 0.0:
                        self.resistance = resistance_candidate
                    else:
                        self.resistance = 0.0
                else:
                    self.resistance = 0.0
                output["calculated_resistance"] = self.resistance
                output["publish_calc_report"] = 1
                self.state = CalcState.MONITOR
            elif self.state == CalcState.MONITOR:
                output["enter_monitor"] = 1
            else:
                self.state = CalcState.WAIT_SAFE

        return output


def assert_equal(actual, expected, label):
    if actual != expected:
        raise AssertionError(f"{label}: expected {expected!r}, got {actual!r}")


def assert_close(actual, expected, label, epsilon=0.0001):
    if abs(actual - expected) > epsilon:
        raise AssertionError(f"{label}: expected {expected!r}, got {actual!r}")


def check_output_protection_behavior():
    print(">>> 正在执行 output_protection 状态机步进用例...")
    
    # 测试链路 1：冷启动自愈流程与上电延时防冲击
    model = OutputProtectionModel()
    assert_equal(model.state, ProtectionState.TRIPPED, "power-on default state is TRIPPED")
    
    # 刚上电，在 100ms 时检测正常，应该转入 RECOVERY_WAIT 并记录 tick
    out = model.update(25.0, 380.0, 0.0, 100)
    assert_equal(model.state, ProtectionState.RECOVERY_WAIT, "safe startup enters recovery wait")
    assert_equal(out["enable_output"], 0, "recovery wait does not enable immediately")
    
    # 观察期中 (未满 2000ms)，保持 RECOVERY_WAIT
    out = model.update(25.0, 380.0, 0.0, 1000)
    assert_equal(model.state, ProtectionState.RECOVERY_WAIT, "still in recovery wait")
    assert_equal(out["enable_output"], 0, "still disabled")
    
    # 观察期满 (2100ms - 100ms = 2000ms)，回到 NORMAL，自动拉高使能
    out = model.update(25.0, 380.0, 0.0, 2100)
    assert_equal(model.state, ProtectionState.NORMAL, "recovery finishes, enters normal")
    assert_equal(out["enable_output"], 1, "recovery completion enables output")
    
    # 处于 NORMAL 状态时，注入超温故障，触发跳闸
    out = model.update(85.5, 380.0, 3.0, 2200)
    assert_equal(model.state, ProtectionState.TRIPPED, "overtemperature trips to TRIPPED")
    assert_equal(out["disable_output"], 1, "trip disables output")
    assert_equal(out["reset_calc_control"], 1, "trip resets calc")

    # 温度回落但仍处于迟滞区间 (75度 - 85度之间)，保持 TRIPPED
    out = model.update(80.0, 380.0, 3.0, 2300)
    assert_equal(model.state, ProtectionState.TRIPPED, "hysteresis keeps tripped")
    assert_equal(out["disable_output"], 1, "tripped state keeps output disabled")

    # 温度彻底安全 (<75度)，再次进入自愈等待
    out = model.update(74.5, 380.0, 3.0, 2400)
    assert_equal(model.state, ProtectionState.RECOVERY_WAIT, "safe values re-enter recovery wait")
    assert_equal(out["enable_output"], 0, "no immediate enable")

    # 在自愈等待中再次遇到故障，被打回 TRIPPED
    out = model.update(86.0, 380.0, 3.0, 3000)
    assert_equal(model.state, ProtectionState.TRIPPED, "fault during recovery wait trips back")
    assert_equal(out["disable_output"], 1, "disables output")

    # 再次安全并正常完成自愈
    out = model.update(74.0, 380.0, 3.0, 3100)
    assert_equal(model.state, ProtectionState.RECOVERY_WAIT, "re-enter recovery wait")
    out = model.update(74.0, 380.0, 3.0, 5099)
    assert_equal(model.state, ProtectionState.RECOVERY_WAIT, "wait full window")
    out = model.update(74.0, 380.0, 3.0, 5100)
    assert_equal(model.state, ProtectionState.NORMAL, "normal recovery finished")
    assert_equal(out["enable_output"], 1, "enables output")

    # 验证其他通道跳闸：从 NORMAL 开始
    for label, temperature, voltage, current in (
        ("overvoltage", 40.0, 420.0, 3.0),
        ("overcurrent", 40.0, 380.0, 12.0),
    ):
        # 建立一个已自愈正常的模型
        m = OutputProtectionModel()
        m.update(25.0, 380.0, 0.0, 0)      # RECOVERY_WAIT, start=0
        m.update(25.0, 380.0, 0.0, 2000)   # NORMAL, enable_output=1
        assert_equal(m.state, ProtectionState.NORMAL, "stabilized to normal")
        
        # 注入故障
        out = m.update(temperature, voltage, current, 2010)
        assert_equal(m.state, ProtectionState.TRIPPED, f"{label} trips")
        assert_equal(out["disable_output"], 1, f"{label} disables output")


def check_calc_control_behavior():
    print(">>> 正在执行 calc_control 九段状态机步进用例...")
    model = CalcControlModel()

    out = model.update(400.0, 0.0, 0, True)
    assert_equal(model.state, CalcState.WAIT_3A_STABLE, "safe start moves to 3A wait")
    assert_equal(out["change_current"], 1, "3A step requests current change")
    assert_close(out["set_current_a"], 3.0, "3A step current")

    out = model.update(399.0, 3.0, 99, True)
    assert_equal(model.state, CalcState.WAIT_3A_STABLE, "3A wait blocks before 100ms")
    assert_equal(out["change_current"], 0, "no current change while waiting 3A")

    out = model.update(399.0, 3.0, 100, True)
    assert_equal(model.state, CalcState.WAIT_2A_STABLE, "3A latch moves to 2A wait")
    assert_close(out["u1"], 399.0, "3A latch voltage")
    assert_close(out["i1"], 3.0, "3A latch current")
    assert_equal(out["change_current"], 1, "2A step requests current change")
    assert_close(out["set_current_a"], 2.0, "2A step current")

    out = model.update(398.0, 2.0, 199, True)
    assert_equal(model.state, CalcState.WAIT_2A_STABLE, "2A wait blocks before 100ms")
    assert_equal(out["publish_calc_report"], 0, "no report before 2A stable")

    out = model.update(398.0, 2.0, 200, True)
    assert_equal(model.state, CalcState.MONITOR, "2A latch calculates and enters monitor")
    assert_equal(out["publish_calc_report"], 1, "calculation publishes report")
    assert_close(out["calculated_resistance"], 1.0, "resistance calculation")

    out = model.update(398.0, 2.0, 210, True)
    assert_equal(model.state, CalcState.MONITOR, "monitor holds")
    assert_equal(out["enter_monitor"], 1, "monitor output flag")

    out = model.update(398.0, 2.0, 220, False)
    assert_equal(model.state, CalcState.WAIT_SAFE, "unsafe input resets calc")
    assert_equal(out["change_current"], 1, "unsafe reset requests DAC zero")
    assert_close(out["set_current_a"], 0.0, "unsafe reset current")

    model = CalcControlModel()
    model.update(400.0, 0.0, 0, True)
    out = model.update(399.0, 3.0, 10, True, reset_request=True)
    assert_equal(model.state, CalcState.WAIT_SAFE, "reset request returns wait safe")
    assert_close(out["calculated_resistance"], 0.0, "reset clears resistance")

    model = CalcControlModel()
    model.update(400.0, 0.0, 0, True)
    model.update(399.0, 2.005, 100, True)
    out = model.update(398.0, 2.0, 200, True)
    assert_equal(model.state, CalcState.MONITOR, "small delta current still completes")
    assert_close(out["calculated_resistance"], 0.0, "small delta current clamps resistance")

    model = CalcControlModel()
    model.update(400.0, 0.0, 0, True)
    model.update(399.0, 2.0, 100, True)
    out = model.update(398.0, 3.0, 200, True)
    assert_equal(model.state, CalcState.MONITOR, "reverse current still completes")
    assert_close(out["calculated_resistance"], 0.0, "reverse current clamps resistance")

    model = CalcControlModel()
    model.update(400.0, 0.0, 0, True)
    model.update(398.0, 3.0, 100, True)
    out = model.update(399.0, 2.0, 200, True)
    assert_equal(model.state, CalcState.MONITOR, "negative resistance candidate still completes")
    assert_close(out["calculated_resistance"], 0.0, "negative resistance candidate clamps resistance")


def main():
    print("==================================================")
    print(" 开始执行宿主机端状态机步进行为校验套件")
    print("==================================================")
    try:
        check_output_protection_behavior()
        check_calc_control_behavior()
    except AssertionError as exc:
        print(f"测试失败: {exc}")
        print("==================================================")
        return 1

    print("==================================================")
    print(" 测试结论: 状态机步进行为校验通过!")
    print("==================================================")
    return 0


if __name__ == "__main__":
    sys.exit(main())
