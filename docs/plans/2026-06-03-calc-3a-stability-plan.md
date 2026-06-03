# Calc 3A Stability Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Align the 3A latch path with the agreed behavior: after entering the 3A step, sample `CO_OUT` every 100ms, require five consecutive current deltas less than or equal to 500mA, treat stable current below 1A as open-circuit, and only then latch `VO_OUT/CO_OUT` as `U1/I1`.

**Architecture:** Keep the stability decision inside `calc_control.c` and leave `StartCalcControlTask()` as the task-level orchestrator. Use `tests/check_state_machines_behavior.py` to lock the expected state-machine behavior first, then mirror the same cadence and open-circuit semantics into firmware, and finish with the project-mandated PowerShell regressions.

**Tech Stack:** STM32G4xx HAL, FreeRTOS (CMSIS-OS V2), Python behavior harness, PowerShell validation scripts.

---

### Task 1: Lock the expected 3A stability behavior in tests

**Files:**
- Modify: `tests/check_state_machines_behavior.py`
- Test: `tests/check_state_machines_behavior.py`

**Step 1: Write the failing test**

Add assertions that cover:

```python
# still waiting before the first 100ms sample point
assert_equal(model.state, CalcState.WAIT_3A_STABLE, "waits until 100ms cadence")

# only enter latch after five consecutive deltas <= 500mA
assert_equal(model.state, CalcState.WAIT_3A_STABLE, "needs five stable windows")

# stable current below 1A is treated as open-circuit
assert_equal(model.state, CalcState.WAIT_SAFE, "low-current stable state resets as open circuit")
```

**Step 2: Run test to verify it fails**

Run: `python tests/check_state_machines_behavior.py`
Expected: FAIL in the 3A stability path because the current host-side model still follows the older fixed-delay behavior.

**Step 3: Write minimal implementation**

Update the host-side `CalcControlModel` in the same file to track:

```python
self.i1_prev_sample_ma
self.i1_last_sample_tick
self.i1_stable_count
self.i1_first_sample
```

and gate the comparison with:

```python
if tick_ms - self.i1_last_sample_tick >= 100:
```

**Step 4: Run test to verify it passes**

Run: `python tests/check_state_machines_behavior.py`
Expected: PASS

**Step 5: Commit**

```bash
git add tests/check_state_machines_behavior.py
git commit -m "test: model 3A dynamic stability behavior"
```

### Task 2: Mirror the accepted timing logic into firmware

**Files:**
- Modify: `Core/Src/calc_control.c`
- Test: `tests/check_state_machines_behavior.py`

**Step 1: Re-run the behavior harness as the reference**

Run: `python tests/check_state_machines_behavior.py`
Expected: PASS, giving the exact behavior the firmware must now match.

**Step 2: Write minimal implementation**

In `Core/Src/calc_control.c`:

```c
if ((input->tick_ms - s_i1_last_sample_tick) >= STEP_3A_SAMPLE_INTERVAL_MS)
{
    delta_ma = fabsf(current_ma - s_i1_prev_sample);
    if (delta_ma <= (float)CURRENT_DELTA_THRESHOLD_MA)
    {
        s_i1_stable_count++;
        if (s_i1_stable_count >= STABLE_CONSECUTIVE_COUNT)
        {
            if (current_ma < (float)CURRENT_MIN_THRESHOLD_MA)
            {
                /* open circuit */
            }
            else
            {
                s_state = CALC_CONTROL_LATCH_3A;
            }
        }
    }
    else
    {
        s_i1_stable_count = 0U;
    }

    s_i1_prev_sample = current_ma;
    s_i1_last_sample_tick = input->tick_ms;
}
```

Keep `LATCH_3A` as a pure latch state; do not move waiting logic into it.

**Step 3: Run test to verify the firmware-facing static regressions still pass**

Run: `powershell -File tests/check_uart_gui_protocol.ps1`
Expected: PASS

**Step 4: Commit**

```bash
git add Core/Src/calc_control.c
git commit -m "fix: sample 3A stability on 100ms cadence"
```

### Task 3: Run the required project regression checks

**Files:**
- Test: `tests/check_adc_config.ps1`
- Test: `tests/check_uart_gui_protocol.ps1`

**Step 1: Run ADC and hardware configuration regression**

Run: `powershell -File tests/check_adc_config.ps1`
Expected: PASS

**Step 2: Run UART, GUI, and state-machine regression**

Run: `powershell -File tests/check_uart_gui_protocol.ps1`
Expected: PASS

**Step 3: Review the resulting diff**

Run: `git diff -- Core/Src/calc_control.c tests/check_state_machines_behavior.py docs/ARCH_documentation-governance.md docs/plans/2026-06-03-calc-3a-stability-plan.md`
Expected: only the agreed timing fix, test updates, and plan registration changes appear.

**Step 4: Commit**

```bash
git add docs/ARCH_documentation-governance.md docs/plans/2026-06-03-calc-3a-stability-plan.md
git commit -m "docs: add 3A stability implementation plan"
```
