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
STEP_2A_STABLE_MS = 10000
CALC_CONTROL_IOC_GAIN_K = 1.0
CALC_CONTROL_IOC_MIN_A = 0.1
CALC_CONTROL_IOC_MAX_A = 15.0


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
        self.i1_prev_sample = 0.0
        self.i1_last_sample_tick = 0
        self.i1_stable_count = 0
        self.open_circuit_error = 0
        self.open_circuit_latched = 0
        self.ioc_target = 0.0
        self.ioc_valid = False
        # 交流线阻限功率闭环：上次锁定阻值及其有效标志
        self.r_last_locked_ohm = 0.0
        self.r_last_valid = False
        # 2A 等待挂起/恢复相关变量
        self.wait2a_paused = False
        self.wait2a_pause_tick = 0

    def pause_wait2a(self, tick_ms):
        """挂起 2A 稳定等待计时"""
        if self.state != CalcState.WAIT_2A_STABLE:
            return False
        if self.wait2a_paused:
            return False
        self.wait2a_paused = True
        self.wait2a_pause_tick = tick_ms
        return True

    def resume_wait2a(self, tick_ms):
        """恢复 2A 稳定等待计时"""
        if not self.wait2a_paused:
            return False
        pause_elapsed = tick_ms - self.wait2a_pause_tick
        self.state_start_tick += pause_elapsed
        self.wait2a_paused = False
        self.wait2a_pause_tick = 0
        return True

    def is_wait2a_paused(self):
        """查询是否处于 2A 等待挂起状态"""
        return self.wait2a_paused

    def update(self, vo_out_v, co_out_a, tick_ms, safe_allowed, reset_request=False,
               vac_ch1_v=0.0, vac_ch2_v=0.0, vdc_sample_v=None):
        if vdc_sample_v is None:
            vdc_sample_v = vo_out_v
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
            "ioc": self.ioc_target if self.ioc_valid else 0.0,
            "ioc_valid": 1 if self.ioc_valid else 0,
            "ioc_updated": 0,
            "stable_update": 0,
            "stable_count": self.i1_stable_count,
            "stable_target": 5,
            "stable_delta_ma": 0.0,
            "open_circuit_error": self.open_circuit_error,
            "iac_limit_a": 0.0,
            "idc_limit_a": 0.0,
            "dac_code": 0,
            "need_retest": 0,
            "du_v": 0.0,
            "di_a": 0.0,
            "r_raw_ohm": 0.0,
            "vac_eff_v": 0.0,
            "vout_avg_v": 0.0,
            "pin_w": 0.0,
            "pout_w": 0.0,
        }

        if reset_request or not safe_allowed:
            self.state = CalcState.WAIT_SAFE
            self.state_start_tick = 0
            self.u1 = 0.0
            self.i1 = 0.0
            self.u2 = 0.0
            self.i2 = 0.0
            self.resistance = 0.0
            self.i1_prev_sample = 0.0
            self.i1_last_sample_tick = 0
            self.i1_stable_count = 0
            self.open_circuit_error = 0
            self.open_circuit_latched = 0
            self.ioc_target = 0.0
            self.ioc_valid = False
            self.r_last_locked_ohm = 0.0
            self.r_last_valid = False
            output["set_current_a"] = 0.0
            output["change_current"] = 1
            output["calculated_resistance"] = 0.0
            output["open_circuit_error"] = 0
            output["ioc"] = 0.0
            output["ioc_valid"] = 0
            return output

        keep_running = True
        while keep_running:
            keep_running = False

            if self.state == CalcState.WAIT_SAFE:
                if safe_allowed and not self.open_circuit_latched:
                    self.state = CalcState.SET_3A
                    keep_running = True
            elif self.state == CalcState.SET_3A:
                output["set_current_a"] = 3.0
                output["change_current"] = 1
                self.state_start_tick = tick_ms
                self.i1_prev_sample = co_out_a
                self.i1_last_sample_tick = tick_ms
                self.i1_stable_count = 0
                self.open_circuit_error = 0
                self.open_circuit_latched = 0
                output["open_circuit_error"] = 0
                self.state = CalcState.WAIT_3A_STABLE
            elif self.state == CalcState.WAIT_3A_STABLE:
                if tick_ms - self.i1_last_sample_tick >= 100:
                    delta_a = abs(co_out_a - self.i1_prev_sample)
                    output["stable_update"] = 1
                    output["stable_delta_ma"] = delta_a * 1000.0
                    if delta_a <= 0.5:
                        self.i1_stable_count += 1
                        output["stable_count"] = self.i1_stable_count
                        if self.i1_stable_count >= 5:
                            if co_out_a < 1.0:
                                self.open_circuit_error = 1
                                self.open_circuit_latched = 1
                                output["open_circuit_error"] = 1
                                self.state = CalcState.WAIT_SAFE
                                keep_running = False
                            else:
                                self.state = CalcState.LATCH_3A
                                keep_running = True
                    else:
                        self.i1_stable_count = 0
                        output["stable_count"] = 0
                    
                    self.i1_prev_sample = co_out_a
                    self.i1_last_sample_tick = tick_ms
            elif self.state == CalcState.LATCH_3A:
                self.u1 = vo_out_v
                self.i1 = co_out_a
                output["u1"] = self.u1
                output["i1"] = self.i1
                self.state = CalcState.SET_2A
                keep_running = True
            elif self.state == CalcState.SET_2A:
                target = self.i1 - 1.0
                if target < 0.1:
                    target = 0.1
                elif target > 15.0:
                    target = 15.0
                output["set_current_a"] = target
                output["change_current"] = 1
                self.state_start_tick = tick_ms
                self.state = CalcState.WAIT_2A_STABLE
            elif self.state == CalcState.WAIT_2A_STABLE:
                if self.wait2a_paused:
                    # 已挂起，不推进计时器
                    break
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
                if di >= 0.2:
                    resistance_candidate = abs(self.u1 - self.u2) / di
                    if resistance_candidate >= 0.0:
                        self.resistance = resistance_candidate
                    else:
                        self.resistance = 0.0
                else:
                    self.resistance = 0.0
                output["calculated_resistance"] = self.resistance
                output["du_v"] = abs(self.u1 - self.u2)
                output["di_a"] = di if di > 0.0 else 0.0
                output["r_raw_ohm"] = self.resistance
                # 交流线阻闭环限功率：用 LineLimitModel 驱动 IOC，取代旧 K/R 逻辑。
                ll_cfg = default_line_limit_config()
                ll_state = new_line_limit_state(
                    self.resistance,
                    self.r_last_locked_ohm,
                    vac_ch1_v,
                    vac_ch2_v,
                    (self.u1 + self.u2) * 0.5,
                    r_last_valid=1 if self.r_last_valid else 0,
                )
                ll_status = LineLimitModel().process(ll_cfg, ll_state)
                output["iac_limit_a"] = ll_state["iac_limited_a"]
                output["idc_limit_a"] = ll_state["idc_max_limit_a"]
                output["dac_code"] = ll_state["dac_code"]
                output["vac_eff_v"] = ll_state["vac_sample_v"]
                output["vout_avg_v"] = ll_state["vdc_sample_v"]
                output["pin_w"] = ll_state["input_power_w"]
                output["pout_w"] = ll_state["output_power_w"]
                if ll_status == LINE_LIMIT_STATUS_OK:
                    self.r_last_locked_ohm = ll_state["r_new_locked_ohm"]
                    self.r_last_valid = True
                    self.ioc_target = ll_state["idc_max_limit_a"]
                    self.ioc_valid = True
                    output["ioc"] = self.ioc_target
                    output["ioc_valid"] = 1
                    output["ioc_updated"] = 1
                    output["set_current_a"] = self.ioc_target
                    output["change_current"] = 1
                    output["need_retest"] = 0
                    output["publish_calc_report"] = 1
                    self.state = CalcState.MONITOR
                else:
                    # NEED_RETEST：清掉本次无效闭环结果，回到 WAIT_SAFE 重新探测。
                    self.ioc_target = 0.0
                    self.ioc_valid = False
                    output["ioc"] = 0.0
                    output["ioc_valid"] = 0
                    output["set_current_a"] = 0.0
                    output["change_current"] = 1
                    output["need_retest"] = 1
                    output["publish_calc_report"] = 1
                    self.state = CalcState.WAIT_SAFE
            elif self.state == CalcState.MONITOR:
                output["enter_monitor"] = 1
                output["ioc"] = self.ioc_target if self.ioc_valid else 0.0
                output["ioc_valid"] = 1 if self.ioc_valid else 0
            else:
                self.state = CalcState.WAIT_SAFE

        return output


LINE_LIMIT_STATUS_OK = 0
LINE_LIMIT_STATUS_NEED_RETEST = 1
LINE_LIMIT_STATUS_INVALID_ARG = 2

# 交流线阻限功率算法默认配置（与 line_limit.c 默认值保持一致）
LINE_LIMIT_IK_CONST = 20.0
LINE_LIMIT_EFFICIENCY = 0.98
LINE_LIMIT_R_MIN_OHM = 0.2
LINE_LIMIT_R_MAX_OHM = 15.0
LINE_LIMIT_R_DELTA_MAX_OHM = 0.5
LINE_LIMIT_SINGLE_CH_MAX_A = 10.0
LINE_LIMIT_DUAL_CH_MAX_A = 15.0
LINE_LIMIT_AC_ONLINE_THRESHOLD_V = 50.0
LINE_LIMIT_MIN_VALID_VAC_V = 50.0
LINE_LIMIT_MIN_VALID_VDC_V = 10.0
LINE_LIMIT_IDC_FULLSCALE_A = 15.0
LINE_LIMIT_DAC_FULLSCALE_CODE = 4095


def default_line_limit_config():
    """构造一份与 C 端默认值同构的配置字典。"""
    return {
        "ik_const": LINE_LIMIT_IK_CONST,
        "efficiency": LINE_LIMIT_EFFICIENCY,
        "r_min_ohm": LINE_LIMIT_R_MIN_OHM,
        "r_max_ohm": LINE_LIMIT_R_MAX_OHM,
        "r_delta_max_ohm": LINE_LIMIT_R_DELTA_MAX_OHM,
        "single_ch_max_a": LINE_LIMIT_SINGLE_CH_MAX_A,
        "dual_ch_max_a": LINE_LIMIT_DUAL_CH_MAX_A,
        "ac_online_threshold_v": LINE_LIMIT_AC_ONLINE_THRESHOLD_V,
        "min_valid_vac_v": LINE_LIMIT_MIN_VALID_VAC_V,
        "min_valid_vdc_v": LINE_LIMIT_MIN_VALID_VDC_V,
        "idc_fullscale_a": LINE_LIMIT_IDC_FULLSCALE_A,
        "dac_fullscale_code": LINE_LIMIT_DAC_FULLSCALE_CODE,
    }


class LineLimitModel:
    """交流线阻闭环限功率算法的宿主机真值参考（5 阶段纯数学推演）。

    与 C 端 Process_AcLine_Limit() 接口同构：输入/配置/中间量全部通过
    state 字典携带，不接触状态机计时，不依赖外部全局变量。
    """

    def process(self, cfg, state):
        # 默认输出清零
        state["online_channel_count"] = 0
        state["r_new_locked_ohm"] = 0.0
        state["iac_theoretical_a"] = 0.0
        state["iac_limited_a"] = 0.0
        state["vac_sample_v"] = 0.0
        state["input_power_w"] = 0.0
        state["output_power_w"] = 0.0
        state["idc_max_limit_a"] = 0.0
        state["dac_code"] = 0

        if cfg is None or state is None:
            return LINE_LIMIT_STATUS_INVALID_ARG

        # 阶段 1：阻抗范围和波动校验
        r_calc = state["r_calc_ohm"]
        if r_calc < cfg["r_min_ohm"] or r_calc > cfg["r_max_ohm"]:
            return LINE_LIMIT_STATUS_NEED_RETEST
        # 仅当上次锁定值有效时才做波动校验；首次测量（无有效历史）跳过。
        r_last_valid = state.get("r_last_valid", 0)
        if r_last_valid and state["r_last_locked_ohm"] > 0.0:
            if abs(r_calc - state["r_last_locked_ohm"]) > cfg["r_delta_max_ohm"]:
                return LINE_LIMIT_STATUS_NEED_RETEST
        state["r_new_locked_ohm"] = r_calc

        # 阶段 2：理论交流最大电流
        if state["r_new_locked_ohm"] <= 0.0:
            return LINE_LIMIT_STATUS_NEED_RETEST
        state["iac_theoretical_a"] = cfg["ik_const"] / state["r_new_locked_ohm"]

        # 阶段 3：单/双通道在线统计
        ch1_online = state["vac_ch1_sample_v"] > cfg["ac_online_threshold_v"]
        ch2_online = state["vac_ch2_sample_v"] > cfg["ac_online_threshold_v"]
        online = (1 if ch1_online else 0) + (1 if ch2_online else 0)
        state["online_channel_count"] = online
        if online == 0:
            return LINE_LIMIT_STATUS_NEED_RETEST
        state["iac_limited_a"] = state["iac_theoretical_a"]

        # 阶段 4：输入功率与输出功率折算
        if online == 2:
            vac_sample = (state["vac_ch1_sample_v"] + state["vac_ch2_sample_v"]) * 0.5
        elif ch1_online:
            vac_sample = state["vac_ch1_sample_v"]
        else:
            vac_sample = state["vac_ch2_sample_v"]
        state["vac_sample_v"] = vac_sample
        if vac_sample < cfg["min_valid_vac_v"] or state["vdc_sample_v"] < cfg["min_valid_vdc_v"]:
            return LINE_LIMIT_STATUS_NEED_RETEST
        state["input_power_w"] = state["iac_theoretical_a"] * vac_sample
        state["output_power_w"] = state["input_power_w"] * cfg["efficiency"]
        idc_power_limit = state["output_power_w"] / state["vdc_sample_v"]
        if online == 2:
            hw_limit = cfg["dual_ch_max_a"]
        else:
            hw_limit = cfg["single_ch_max_a"]
        state["idc_max_limit_a"] = min(idc_power_limit, hw_limit)

        # 阶段 5：DAC 量化
        fullscale_a = cfg["idc_fullscale_a"]
        fullscale_code = cfg["dac_fullscale_code"]
        if state["idc_max_limit_a"] >= fullscale_a:
            state["dac_code"] = fullscale_code
        else:
            state["dac_code"] = int(
                (state["idc_max_limit_a"] * fullscale_code / fullscale_a) + 0.5
            )

        return LINE_LIMIT_STATUS_OK


def new_line_limit_state(r_calc_ohm, r_last_locked_ohm,
                         vac_ch1_sample_v, vac_ch2_sample_v, vdc_sample_v,
                         r_last_valid=0):
    return {
        "r_calc_ohm": r_calc_ohm,
        "r_last_locked_ohm": r_last_locked_ohm,
        "r_last_valid": r_last_valid,
        "vac_ch1_sample_v": vac_ch1_sample_v,
        "vac_ch2_sample_v": vac_ch2_sample_v,
        "vdc_sample_v": vdc_sample_v,
        "online_channel_count": 0,
        "r_new_locked_ohm": 0.0,
        "iac_theoretical_a": 0.0,
        "iac_limited_a": 0.0,
        "vac_sample_v": 0.0,
        "input_power_w": 0.0,
        "output_power_w": 0.0,
        "idc_max_limit_a": 0.0,
        "dac_code": 0,
    }


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
    
    # 1. 正常 3A 判稳和计算内阻流程
    model = CalcControlModel()

    # 0ms: 触发进入 WAIT_3A_STABLE，且 co_out_a = 0.0A
    out = model.update(400.0, 0.0, 0, True)
    assert_equal(model.state, CalcState.WAIT_3A_STABLE, "safe start moves to 3A wait")
    assert_equal(out["change_current"], 1, "3A step requests current change")
    assert_close(out["set_current_a"], 3.0, "3A step current")

    # 99ms: 不到 100ms 采样点，状态不变，保持 WAIT_3A_STABLE
    out = model.update(399.0, 3.0, 99, True)
    assert_equal(model.state, CalcState.WAIT_3A_STABLE, "3A wait blocks before 100ms")

    # 100ms: 第 1 次采样。如果此时 co_out_a = 2.5A。上次采样 co_out_a = 0.0A，delta = 2.5A > 0.5A，未稳定，计数器归零
    out = model.update(399.0, 2.5, 100, True)
    assert_equal(model.state, CalcState.WAIT_3A_STABLE, "1st sample delta > 500mA, not stable")

    # 200ms: 第 2 次采样。co_out_a = 3.0A。delta = 0.5A <= 0.5A，stable_count = 1
    out = model.update(399.0, 3.0, 200, True)
    assert_equal(model.state, CalcState.WAIT_3A_STABLE, "2nd sample delta <= 500mA, count=1")
    assert_equal(out["stable_update"], 1, "stable telemetry updates on sample points")
    assert_equal(out["stable_count"], 1, "stable telemetry reports incremented count")

    # 300ms: 第 3 次采样。co_out_a = 3.1A。delta = 0.1A <= 0.5A，stable_count = 2
    out = model.update(399.0, 3.1, 300, True)
    assert_equal(model.state, CalcState.WAIT_3A_STABLE, "3rd sample delta <= 500mA, count=2")
    assert_equal(out["stable_count"], 2, "stable telemetry follows consecutive count")

    # 400ms: 第 4 次采样。co_out_a = 2.9A。delta = 0.2A <= 0.5A，stable_count = 3
    out = model.update(399.0, 2.9, 400, True)
    assert_equal(model.state, CalcState.WAIT_3A_STABLE, "4th sample delta <= 500mA, count=3")

    # 500ms: 第 5 次采样。co_out_a = 3.0A。delta = 0.1A <= 0.5A，stable_count = 4
    out = model.update(399.0, 3.0, 500, True)
    assert_equal(model.state, CalcState.WAIT_3A_STABLE, "5th sample delta <= 500mA, count=4")

    # 600ms: 第 6 次采样。co_out_a = 3.0A。delta = 0.0A <= 0.5A，stable_count = 5
    # 达到 5 次稳定，且电流 3.0A >= 1.0A，进入 LATCH_3A -> SET_2A -> WAIT_2A_STABLE
    # 所以更新后状态应当是 WAIT_2A_STABLE
    out = model.update(399.0, 3.0, 600, True)
    assert_equal(model.state, CalcState.WAIT_2A_STABLE, "5 consecutive stable samples >= 1A triggers 2A step")
    assert_close(out["u1"], 399.0, "3A latch voltage")
    assert_close(out["i1"], 3.0, "3A latch current")
    assert_equal(out["change_current"], 1, "2A step requests current change")
    assert_close(out["set_current_a"], 2.0, "2A step current")

    # 2A 稳定判定：等待 10s 消抖
    # 10599ms: 不足 10s
    out = model.update(398.0, 2.0, 10599, True)
    assert_equal(model.state, CalcState.WAIT_2A_STABLE, "2A wait blocks before 10s")

    # 10600ms: 满 10s，锁存 -> 计算内阻 -> 交流线阻闭环 -> MONITOR
    # 新功率公式：R=1.0 -> iac=20A，Pin=20*200，Pout=Pin*0.98，
    # Vout_avg=(399+398)/2=398.5V，IOC=min(Pout/Vout_avg, 10A)=约 9.8369A。
    out = model.update(398.0, 2.0, 10600, True,
                       vac_ch1_v=200.0, vac_ch2_v=0.0, vdc_sample_v=400.0)
    assert_equal(model.state, CalcState.MONITOR, "2A latch calculates and enters monitor")
    assert_equal(out["publish_calc_report"], 1, "calculation publishes report")
    assert_close(out["calculated_resistance"], 1.0, "resistance calculation")
    assert_close(out["du_v"], 1.0, "debug du captures latch voltage delta magnitude")
    assert_close(out["di_a"], 1.0, "debug di captures latch current delta")
    assert_close(out["r_raw_ohm"], 1.0, "debug raw resistance is published")
    assert_close(out["vac_eff_v"], 200.0, "debug effective Vac follows online channel")
    assert_close(out["vout_avg_v"], (399.0 + 398.0) * 0.5, "debug average Vout follows latch average")
    assert_close(out["pin_w"], 20.0 * 200.0, "debug input power is published")
    assert_close(out["pout_w"], 20.0 * 200.0 * 0.98, "debug output power is published")
    assert_equal(out["need_retest"], 0, "valid closed-loop does not request retest")
    assert_equal(out["ioc_updated"], 1, "valid resistance publishes one IOC update pulse")
    assert_equal(out["ioc_valid"], 1, "valid resistance enables closed-loop IOC")
    assert_close(out["ioc"], (20.0 * 200.0 * 0.98) / ((399.0 + 398.0) * 0.5), "closed-loop IOC follows power-based line-limit")
    assert_close(out["idc_limit_a"], (20.0 * 200.0 * 0.98) / ((399.0 + 398.0) * 0.5), "line-limit Idc output")
    assert_equal(out["change_current"], 1, "closed-loop handoff requests DAC rewrite")
    assert_close(out["set_current_a"], (20.0 * 200.0 * 0.98) / ((399.0 + 398.0) * 0.5), "closed-loop handoff rewrites DAC to IOC target")

    # MONITOR 状态保持
    out = model.update(398.0, 2.0, 710, True)
    assert_equal(model.state, CalcState.MONITOR, "monitor holds")
    assert_equal(out["enter_monitor"], 1, "monitor output flag")
    assert_equal(out["ioc_updated"], 0, "monitor hold does not continuously republish IOC update")
    assert_equal(out["change_current"], 0, "monitor hold does not continuously rewrite DAC")
    assert_equal(out["ioc_valid"], 1, "monitor keeps closed-loop IOC valid")
    assert_close(out["ioc"], (20.0 * 200.0 * 0.98) / ((399.0 + 398.0) * 0.5), "monitor holds last valid IOC target")

    # 保护退出或重置
    out = model.update(398.0, 2.0, 720, False)
    assert_equal(model.state, CalcState.WAIT_SAFE, "unsafe input resets calc")
    assert_equal(out["change_current"], 1, "unsafe reset requests DAC zero")
    assert_close(out["set_current_a"], 0.0, "unsafe reset current")
    assert_equal(out["ioc_valid"], 0, "unsafe reset invalidates cached IOC target")
    assert_close(out["ioc"], 0.0, "unsafe reset clears visible IOC target")

    # 2. 开路检测测试
    model = CalcControlModel()
    # 0ms: 触发进入 WAIT_3A_STABLE，co_out_a = 0.0A
    model.update(400.0, 0.0, 0, True)
    
    # 连续 5 次 stable，但电流极低 (0.1A = 100mA < 1A)
    # 100ms, 200ms, 300ms, 400ms
    for t in [100, 200, 300, 400]:
        out = model.update(400.0, 0.1, t, True)
        assert_equal(model.state, CalcState.WAIT_3A_STABLE, f"stable counting in progress, tick={t}")
    
    # 500ms: 连续 5 次稳定完成，由于 0.1A < 1.0A，触发开路检测，复位回到 WAIT_SAFE
    out = model.update(400.0, 0.1, 500, True)
    assert_equal(model.state, CalcState.WAIT_SAFE, "low-current stable state resets to WAIT_SAFE (open-circuit)")
    assert_equal(out.get("open_circuit_error", 0), 1, "open circuit error is set")
    # 开路时不强制归零电流
    assert_equal(out["change_current"], 0, "open circuit does not trigger current change")
    assert_equal(model.open_circuit_latched, 1, "open circuit result latches retry inhibit")

    # 510ms: 安全条件保持不变时，不应自动重新切回 SET_3A
    out = model.update(400.0, 0.1, 510, True)
    assert_equal(model.state, CalcState.WAIT_SAFE, "open circuit stays in WAIT_SAFE until explicit reset or unsafe transition")
    assert_equal(out["change_current"], 0, "latched open circuit does not auto-drive current again")

    # 3. reset_request 测试
    model = CalcControlModel()
    model.update(400.0, 0.0, 0, True)
    out = model.update(399.0, 3.0, 10, True, reset_request=True)
    assert_equal(model.state, CalcState.WAIT_SAFE, "reset request returns wait safe")
    assert_close(out["calculated_resistance"], 0.0, "reset clears resistance")
    assert_equal(out["ioc_valid"], 0, "reset request clears closed-loop validity")
    assert_close(out["ioc"], 0.0, "reset request clears IOC target")

    # 4. 验证动态设定值的限幅以及差值信噪比保护
    # 情况 A：I1 = 1.0A，设定值降至 1.0 - 1.0 = 0A -> 限幅到 0.1A
    m = CalcControlModel()
    m.update(400.0, 0.0, 0, True)
    for t in [100, 200, 300, 400, 500, 600]:
        out = m.update(399.0, 1.0, t, True)
    assert_equal(m.state, CalcState.WAIT_2A_STABLE, "1.0A stable triggers 2A step")
    assert_close(out["set_current_a"], 0.1, "I1-1.0=0.0 clamped to 0.1A limit")

    # 情况 B：I1 = 16.5A，设定值降至 16.5 - 1.0 = 15.5A -> 限幅到 15.0A
    m = CalcControlModel()
    m.update(400.0, 0.0, 0, True)
    for t in [100, 200, 300, 400, 500, 600]:
        out = m.update(399.0, 16.5, t, True)
    assert_equal(m.state, CalcState.WAIT_2A_STABLE, "16.5A stable triggers 2A step")
    assert_close(out["set_current_a"], 15.0, "I1-1.0=15.5 clamped to 15.0A limit")

    # 情况 C：实际差值 di < 0.2A (例如稳定在 2.9A，di = 3.0 - 2.9 = 0.1A)
    m = CalcControlModel()
    m.update(400.0, 0.0, 0, True)
    for t in [100, 200, 300, 400, 500, 600]:
        out = m.update(399.0, 3.0, t, True)
    m.update(398.0, 2.9, 10599, True)
    out = m.update(398.0, 2.9, 10600, True,
                   vac_ch1_v=200.0, vac_ch2_v=0.0, vdc_sample_v=400.0)
    assert_equal(m.state, CalcState.WAIT_SAFE, "invalid resistance forces retest back to WAIT_SAFE")
    assert_close(out["calculated_resistance"], 0.0, "di=0.1A < 0.2A drops resistance to 0.0R")
    assert_equal(out["need_retest"], 1, "out-of-range resistance requests retest")
    assert_equal(out["ioc_valid"], 0, "invalid resistance must not enable closed-loop IOC")
    assert_close(out["ioc"], 0.0, "invalid resistance reports zero IOC target")
    assert_equal(out["change_current"], 1, "invalid resistance actively withdraws test current")
    assert_close(out["set_current_a"], 0.0, "invalid resistance withdraws to 0A")

    # 情况 D：内阻低于线阻下限 (R=0.05 < 0.2) -> 线阻闭环判定 NEED_RETEST
    m = CalcControlModel()
    m.update(400.0, 0.0, 0, True)
    for t in [100, 200, 300, 400, 500, 600]:
        out = m.update(399.95, 3.0, t, True)
    m.update(399.90, 2.0, 10599, True)
    out = m.update(399.90, 2.0, 10600, True,
                   vac_ch1_v=200.0, vac_ch2_v=0.0, vdc_sample_v=400.0)
    assert_close(out["calculated_resistance"], 0.05, "small resistance scenario is calculated")
    assert_equal(m.state, CalcState.WAIT_SAFE, "below-min resistance forces retest")
    assert_equal(out["need_retest"], 1, "below-min resistance requests retest")
    assert_equal(out["ioc_valid"], 0, "below-min resistance disables closed-loop IOC")
    assert_close(out["ioc"], 0.0, "below-min resistance clears IOC target")

    # 情况 E：内阻高于线阻上限 (R=20 > 15) -> 线阻闭环判定 NEED_RETEST
    m = CalcControlModel()
    m.update(400.0, 0.0, 0, True)
    for t in [100, 200, 300, 400, 500, 600]:
        out = m.update(380.0, 3.0, t, True)
    m.update(360.0, 2.0, 10599, True)
    out = m.update(360.0, 2.0, 10600, True,
                   vac_ch1_v=200.0, vac_ch2_v=0.0, vdc_sample_v=400.0)
    assert_close(out["calculated_resistance"], 20.0, "large resistance scenario is calculated")
    assert_equal(m.state, CalcState.WAIT_SAFE, "above-max resistance forces retest")
    assert_equal(out["need_retest"], 1, "above-max resistance requests retest")
    assert_equal(out["ioc_valid"], 0, "above-max resistance disables closed-loop IOC")
    assert_close(out["ioc"], 0.0, "above-max resistance clears IOC target")

    # 情况 F：合法内阻按功率公式折算，双通道未触发 15A IOC 封顶
    # 构造 R=1.0（u1-u2=1.0, di=1.0），双通道在线 230/230, Vdc=400
    # iac=ik/r=20A，Pin=20*230，Pout=Pin*0.98，Vout_avg=(399+398)/2=398.5V，
    # IOC=11.305A < 15A -> 线性量化不饱和。
    m = CalcControlModel()
    m.update(400.0, 0.0, 0, True)
    for t in [100, 200, 300, 400, 500, 600]:
        out = m.update(399.0, 3.0, t, True)
    m.update(398.0, 2.0, 10599, True)
    out = m.update(398.0, 2.0, 10600, True,
                   vac_ch1_v=230.0, vac_ch2_v=230.0, vdc_sample_v=400.0)
    assert_close(out["calculated_resistance"], 1.0, "valid resistance scenario is calculated")
    assert_equal(m.state, CalcState.MONITOR, "valid closed-loop enters monitor")
    assert_equal(out["need_retest"], 0, "valid closed-loop does not request retest")
    assert_close(out["idc_limit_a"], (20.0 * 230.0 * 0.98) / ((399.0 + 398.0) * 0.5), "line-limit Idc follows power formula")
    assert_close(out["ioc"], (20.0 * 230.0 * 0.98) / ((399.0 + 398.0) * 0.5), "IOC follows power-based Idc")
    assert_equal(out["dac_code"], 3088, "Idc below 15A full-scale stays in linear DAC range")


def check_pause_resume_2a_behavior():
    print(">>> 正在执行 2A 等待挂起/恢复功能校验...")

    # 1. 正常流程进入 WAIT_2A_STABLE 并挂起
    model = CalcControlModel()
    # 快速通过 3A 稳定判定
    model.update(400.0, 0.0, 0, True)
    for t in [100, 200, 300, 400, 500, 600]:
        out = model.update(399.0, 3.0, t, True)
    assert_equal(model.state, CalcState.WAIT_2A_STABLE, "enters WAIT_2A_STABLE after 3A stable")

    # 在 WAIT_2A_STABLE 状态挂起（tick=700）
    result = model.pause_wait2a(700)
    assert_equal(result, True, "pause succeeds in WAIT_2A_STABLE")
    assert_equal(model.is_wait2a_paused(), True, "pause flag is set")
    assert_equal(model.state, CalcState.WAIT_2A_STABLE, "state unchanged after pause")

    # 挂起后多次更新，状态不变
    out = model.update(398.0, 2.0, 800, True)
    assert_equal(model.state, CalcState.WAIT_2A_STABLE, "state holds while paused")
    out = model.update(398.0, 2.0, 900, True)
    assert_equal(model.state, CalcState.WAIT_2A_STABLE, "state still holds at 900ms")
    out = model.update(398.0, 2.0, 1000, True)
    assert_equal(model.state, CalcState.WAIT_2A_STABLE, "state still holds at 1000ms (would be 300ms elapsed, not 1000ms)")

    # 恢复计时（tick=1000），挂起了 300ms（1000-700），state_start_tick 被追加
    # 原始 state_start_tick = 600，追加 300 后 = 900
    # 继续等待 10s（STEP_2A_STABLE_MS）后应触发转移
    result = model.resume_wait2a(1000)
    assert_equal(result, True, "resume succeeds")
    assert_equal(model.is_wait2a_paused(), False, "pause flag is cleared")

    # 恢复后 state_start_tick=900，需到 tick=900+10000=10900 才满足 10s 消抖
    out = model.update(398.0, 2.0, 10900, True,
                       vac_ch1_v=200.0, vac_ch2_v=0.0, vdc_sample_v=400.0)
    assert_equal(model.state, CalcState.MONITOR, "resumed state advances through LATCH_2A after remaining wait")
    assert_equal(out["publish_calc_report"], 1, "calculation publishes after resume")

    # 再次 tick 时应保持 MONITOR，而不是丢失恢复后的状态语义
    out = model.update(398.0, 2.0, 10910, True)
    assert_equal(model.state, CalcState.MONITOR, "monitor remains after resume completion")
    assert_equal(out["enter_monitor"], 1, "monitor hold remains visible after resume")

    # 2. 在非 WAIT_2A_STABLE 状态挂起应失败
    model = CalcControlModel()
    model.update(400.0, 0.0, 0, True)  # WAIT_3A_STABLE
    result = model.pause_wait2a(100)
    assert_equal(result, False, "pause fails in non-WAIT_2A_STABLE state")
    assert_equal(model.is_wait2a_paused(), False, "pause flag not set")

    # 3. 未挂起时恢复应失败
    model = CalcControlModel()
    model.update(400.0, 0.0, 0, True)
    for t in [100, 200, 300, 400, 500, 600]:
        out = model.update(399.0, 3.0, t, True)
    assert_equal(model.state, CalcState.WAIT_2A_STABLE, "enters WAIT_2A_STABLE")
    result = model.resume_wait2a(700)
    assert_equal(result, False, "resume fails when not paused")
    assert_equal(model.is_wait2a_paused(), False, "pause flag not set")

    # 4. 重复挂起应失败
    model.pause_wait2a(700)
    result = model.pause_wait2a(800)
    assert_equal(result, False, "double pause fails")

    # 5. 挂起恢复后完整内阻计算流程
    model = CalcControlModel()
    model.update(400.0, 0.0, 0, True)
    for t in [100, 200, 300, 400, 500, 600]:
        out = model.update(399.0, 3.0, t, True)
    assert_equal(model.state, CalcState.WAIT_2A_STABLE, "3A complete")

    # 挂起
    model.pause_wait2a(650)
    # 模拟长时间挂起（500ms）
    model.resume_wait2a(1150)

    # 恢复后完成剩余等待
    # 挂起了 500ms (1150-650)，state_start_tick 追加后 = 1100
    # 继续等待 10s（STEP_2A_STABLE_MS）后应触发转移：tick=1100+10000=11100
    out = model.update(398.0, 2.0, 11100, True,
                       vac_ch1_v=200.0, vac_ch2_v=0.0, vdc_sample_v=400.0)
    assert_equal(model.state, CalcState.MONITOR, "complete flow after pause/resume")
    assert_close(out["calculated_resistance"], 1.0, "resistance calculated correctly after pause/resume")
    assert_equal(out["need_retest"], 0, "pause/resume flow keeps valid closed-loop")



def check_calc_line_limit_integration():
    print(">>> 正在执行 calc_control 与交流线阻闭环集成校验...")

    def drive_to_calc(model, vac_ch1, vac_ch2, vdc, base_tick=0):
        """把模型从 WAIT_SAFE 推进到 CALC_RESISTANCE 完成的那一拍并返回 output。
        采用与既有 calc_control 用例一致的 3A->2A 时序（u1=399,i1=3; u2=398,i2=2 -> R=1.0）。
        """
        t = base_tick
        model.update(400.0, 0.0, t, True)
        for _ in range(6):
            t += 100
            model.update(399.0, 3.0, t, True)
        model.update(398.0, 2.0, t + STEP_2A_STABLE_MS - 1, True)
        return model.update(398.0, 2.0, t + STEP_2A_STABLE_MS, True,
                            vac_ch1_v=vac_ch1, vac_ch2_v=vac_ch2, vdc_sample_v=vdc)

    # 1. 单通道在线，闭环成功：R=1.0 -> iac=20A，Pin=20*200，Pout=Pin*0.98，
    # Vout_avg=(399+398)/2=398.5V，IOC=min(Pout/Vout_avg,10A)=约9.8369A
    m = CalcControlModel()
    out = drive_to_calc(m, 200.0, 0.0, 400.0)
    assert_equal(m.state, CalcState.MONITOR, "integration: valid single-channel enters monitor")
    assert_equal(out["need_retest"], 0, "integration: valid case no retest")
    assert_close(out["iac_limit_a"], 20.0, "integration: theoretical input current follows ik/r")
    assert_close(out["idc_limit_a"], (20.0 * 200.0 * 0.98) / ((399.0 + 398.0) * 0.5), "integration: Idc follows power formula")
    assert_close(out["ioc"], (20.0 * 200.0 * 0.98) / ((399.0 + 398.0) * 0.5), "integration: IOC output equals power-based Idc")
    assert_equal(out["dac_code"], 2685, "integration: DAC linear quantization at power-based IOC")
    assert_equal(m.r_last_valid, True, "integration: first valid lock records history")
    assert_close(m.r_last_locked_ohm, 1.0, "integration: locked resistance recorded")

    # 2. 无交流通道在线 -> NEED_RETEST，回退到 WAIT_SAFE 并清空闭环
    m = CalcControlModel()
    out = drive_to_calc(m, 10.0, 10.0, 400.0)
    assert_equal(m.state, CalcState.WAIT_SAFE, "integration: no online AC falls back to WAIT_SAFE")
    assert_equal(out["need_retest"], 1, "integration: no online AC requests retest")
    assert_equal(out["ioc_valid"], 0, "integration: retest clears IOC validity")
    assert_close(out["ioc"], 0.0, "integration: retest clears IOC target")
    assert_equal(m.r_last_valid, False, "integration: failed closed-loop does not record history")

    # 3. NEED_RETEST 后重新探测：补足 Vac 后再次走完闭环应成功
    out2 = drive_to_calc(m, 200.0, 0.0, 400.0, base_tick=2000)
    assert_equal(m.state, CalcState.MONITOR, "integration: retest succeeds and enters monitor")
    assert_equal(out2["need_retest"], 0, "integration: successful retest clears retest flag")
    assert_close(out2["ioc"], (20.0 * 200.0 * 0.98) / ((399.0 + 398.0) * 0.5), "integration: retest produces valid IOC")

    # 4. 平均输出电压过低 -> NEED_RETEST 回退
    m = CalcControlModel()
    t = 0
    m.update(20.0, 0.0, t, True)
    for _ in range(6):
        t += 100
        m.update(9.0, 3.0, t, True)
    m.update(8.0, 2.0, t + STEP_2A_STABLE_MS - 1, True)
    out = m.update(8.0, 2.0, t + STEP_2A_STABLE_MS, True,
                   vac_ch1_v=200.0, vac_ch2_v=0.0, vdc_sample_v=400.0)
    assert_equal(m.state, CalcState.WAIT_SAFE, "integration: low output voltage falls back to WAIT_SAFE")
    assert_equal(out["need_retest"], 1, "integration: low output voltage requests retest")

    # 5. MONITOR 保持：成功闭环后再 tick 应维持 MONITOR 与上次 IOC
    m = CalcControlModel()
    drive_to_calc(m, 200.0, 0.0, 400.0)
    out = m.update(398.0, 2.0, 5000, True)
    assert_equal(m.state, CalcState.MONITOR, "integration: monitor holds after closed-loop")
    assert_equal(out["enter_monitor"], 1, "integration: monitor flag set")
    assert_equal(out["change_current"], 0, "integration: monitor hold does not rewrite DAC")
    assert_close(out["ioc"], (20.0 * 200.0 * 0.98) / ((399.0 + 398.0) * 0.5), "integration: monitor holds last IOC")

    print("    calc_control 与交流线阻闭环集成校验通过。")


def check_line_limit_behavior():
    print(">>> 正在执行交流线阻闭环限功率算法校验...")

    cfg = default_line_limit_config()
    model = LineLimitModel()

    # 1. 阻抗超出 [0.2, 15.0] 区间 -> 强制重测
    st = new_line_limit_state(20.0, 0.0, 230.0, 230.0, 400.0)
    status = model.process(cfg, st)
    assert_equal(status, LINE_LIMIT_STATUS_NEED_RETEST, "out-of-range resistance forces retest")

    st = new_line_limit_state(0.05, 0.0, 230.0, 230.0, 400.0)
    status = model.process(cfg, st)
    assert_equal(status, LINE_LIMIT_STATUS_NEED_RETEST, "below-min resistance forces retest")

    # 2. 阻抗相对上次锁定值跳变超过 0.5R -> 强制重测（需上次锁定值有效）
    st = new_line_limit_state(2.0, 1.0, 230.0, 230.0, 400.0, r_last_valid=1)
    status = model.process(cfg, st)
    assert_equal(status, LINE_LIMIT_STATUS_NEED_RETEST, "resistance jump above 0.5R forces retest")

    # 2b. 首次测量（上次锁定值无效）跳过波动校验，即使与 0 差值很大也应通过
    st = new_line_limit_state(1.0, 0.0, 230.0, 230.0, 400.0, r_last_valid=0)
    status = model.process(cfg, st)
    assert_equal(status, LINE_LIMIT_STATUS_OK, "first valid measurement skips delta check")

    # 3. 双通道在线：Vac 取平均值，输入电流保持 ik/r，最终 IOC 由功率法求得
    st = new_line_limit_state(1.0, 1.0, 220.0, 240.0, 400.0)
    status = model.process(cfg, st)
    assert_equal(status, LINE_LIMIT_STATUS_OK, "dual-channel nominal case is OK")
    assert_equal(st["online_channel_count"], 2, "dual-channel counts two online")
    assert_close(st["vac_sample_v"], (220.0 + 240.0) / 2.0, "dual-channel uses average Vac")
    assert_close(st["iac_theoretical_a"], 20.0, "theoretical Iac = ik/r")
    assert_close(st["idc_max_limit_a"], (20.0 * 230.0 * 0.98) / 400.0, "dual-channel IOC follows power formula")

    # 4. 单通道在线：Vac 取在线通道，最终 IOC 取功率法与 10A 上限的较小值
    st = new_line_limit_state(1.0, 1.0, 230.0, 0.0, 400.0)
    status = model.process(cfg, st)
    assert_equal(status, LINE_LIMIT_STATUS_OK, "single-channel nominal case is OK")
    assert_equal(st["online_channel_count"], 1, "single-channel counts one online")
    assert_close(st["vac_sample_v"], 230.0, "single-channel uses the online channel Vac")
    assert_close(st["idc_max_limit_a"], 10.0, "single-channel IOC caps at 10A")

    # 5. 无通道在线 -> 强制重测
    st = new_line_limit_state(1.0, 1.0, 10.0, 10.0, 400.0)
    status = model.process(cfg, st)
    assert_equal(status, LINE_LIMIT_STATUS_NEED_RETEST, "no online channel forces retest")

    # 6. Vdc 过低 -> 强制重测
    st = new_line_limit_state(1.0, 1.0, 230.0, 230.0, 5.0)
    status = model.process(cfg, st)
    assert_equal(status, LINE_LIMIT_STATUS_NEED_RETEST, "low Vdc forces retest")

    # 7. 双通道功率法算得 IOC 超过 15A -> 最终 IOC 封顶到 15A，DAC 满码
    # iac=20A, Vac=400, Vout=200 -> Idc_power = 20*400*0.98/200 = 39.2A -> cap 到 15A
    st = new_line_limit_state(1.0, 1.0, 400.0, 400.0, 200.0)
    status = model.process(cfg, st)
    assert_equal(status, LINE_LIMIT_STATUS_OK, "saturation case is OK")
    assert_close(st["idc_max_limit_a"], 15.0, "Idc output current caps at 15A")
    assert_equal(st["dac_code"], 4095, "Idc above 15A saturates DAC")

    # 8. DAC 线性量化（非饱和区）
    # 选取 idc 恰为 5A -> code = round(5*4095/15) = 1365
    # 构造：R=2.0 -> iac=10A, 单通道 Vac=200, Vout=392 -> idc = 10*200*0.98/392 = 5A
    st = new_line_limit_state(2.0, 2.0, 200.0, 0.0, 392.0)
    status = model.process(cfg, st)
    assert_equal(status, LINE_LIMIT_STATUS_OK, "linear-region case is OK")
    assert_close(st["idc_max_limit_a"], 5.0, "Idc follows power formula to 5A")
    assert_equal(st["dac_code"], 1365, "DAC线性量化(含四舍五入)")

    # 9. 直接校验用户给出的双路样例
    st = new_line_limit_state(1.099, 1.099, 311.0, 311.0, 380.5)
    status = model.process(cfg, st)
    assert_equal(status, LINE_LIMIT_STATUS_OK, "user dual-channel example is OK")
    assert_close(st["iac_theoretical_a"], 20.0 / 1.099, "dual example theoretical input current")
    assert_close(st["idc_max_limit_a"], ((20.0 / 1.099) * 311.0 * 0.98) / 380.5, "dual example IOC follows power formula")

    # 10. 直接校验用户给出的单路样例
    st = new_line_limit_state(0.98, 0.98, 311.0, 0.0, 380.5)
    status = model.process(cfg, st)
    assert_equal(status, LINE_LIMIT_STATUS_OK, "user single-channel example is OK")
    assert_close(st["iac_theoretical_a"], 20.0 / 0.98, "single example theoretical input current")
    assert_close(st["idc_max_limit_a"], 10.0, "single example IOC caps at 10A")

    print("    交流线阻闭环限功率算法校验通过。")


def main():
    print("==================================================")
    print(" 开始执行宿主机端状态机步进行为校验套件")
    print("==================================================")
    try:
        check_output_protection_behavior()
        check_calc_control_behavior()
        check_pause_resume_2a_behavior()
        check_calc_line_limit_integration()
        check_line_limit_behavior()
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
