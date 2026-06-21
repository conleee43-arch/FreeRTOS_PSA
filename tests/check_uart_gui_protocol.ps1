$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$firmwarePath = Join-Path $repoRoot 'Core/Src/app_freertos.c'
$guiPath = Join-Path $repoRoot 'gui/main.py'
$interfacePath = Join-Path $repoRoot 'docs/STANDARDS_interface.md'

$behaviorPy = Join-Path $PSScriptRoot 'check_state_machines_behavior.py'
if (Test-Path -LiteralPath $behaviorPy) {
    python $behaviorPy
    if ($LASTEXITCODE -ne 0) {
        throw "Behavioral check of state machines and algorithm logic failed!"
    }
} else {
    throw "Required behavioral test file is missing: check_state_machines_behavior.py"
}

$guiRuntimeBehavior = Join-Path $PSScriptRoot 'check_gui_runtime_behavior.ps1'
if (Test-Path -LiteralPath $guiRuntimeBehavior) {
    powershell -File $guiRuntimeBehavior
    if ($LASTEXITCODE -ne 0) {
        throw "GUI runtime layout/ack behavior check failed!"
    }
} else {
    throw "Required GUI runtime behavior test file is missing: check_gui_runtime_behavior.ps1"
}

$firmware = Get-Content -Raw -LiteralPath $firmwarePath
$gui = Get-Content -Raw -LiteralPath $guiPath
$interface = Get-Content -Raw -LiteralPath $interfacePath
$calcControlPath = Join-Path $repoRoot 'Core/Src/calc_control.c'
$calcControl = Get-Content -Raw -LiteralPath $calcControlPath

$patternMatch = [regex]::Match(
    $gui,
    'MEASURE_PATTERN\s*=\s*re\.compile\(\s*r"(?<pattern>[^"]+)"\s*\)',
    [System.Text.RegularExpressions.RegexOptions]::Singleline
)

if (-not $patternMatch.Success) {
    throw 'GUI MEASURE_PATTERN was not found in gui/main.py.'
}

$guiPattern = $patternMatch.Groups['pattern'].Value
$sampleMeasureLine = '[Measure] V1:223.90V V2:223.90V CO:10.80A VO:360.00V T:25.0C Vref:2.500V'
$sampleMatch = [regex]::Match($sampleMeasureLine, $guiPattern)

if (-not $sampleMatch.Success -or $sampleMatch.Groups.Count -ne 7) {
    throw "GUI MEASURE_PATTERN does not parse the expected Measure line: $sampleMeasureLine"
}

$calcPatternMatch = [regex]::Match(
    $gui,
    'CALC_PATTERN\s*=\s*re\.compile\(\s*r"(?<pattern>[^"]+)"\s*\)',
    [System.Text.RegularExpressions.RegexOptions]::Singleline
)

if (-not $calcPatternMatch.Success) {
    throw 'GUI CALC_PATTERN was not found in gui/main.py.'
}

$sampleCalcLine = '[Calc] R:1.000R U1:400.00V I1:3.00A U2:399.00V I2:2.00A IOC:1.00A'
$sampleCalcMatch = [regex]::Match($sampleCalcLine, $calcPatternMatch.Groups['pattern'].Value)

if (-not $sampleCalcMatch.Success -or $sampleCalcMatch.Groups.Count -ne 7) {
    throw "GUI CALC_PATTERN does not parse the expected Calc line: $sampleCalcLine"
}

$statePatternMatch = [regex]::Match(
    $gui,
    'STATE_PATTERN\s*=\s*re\.compile\(\s*r"(?<pattern>[^"]+)"\s*\)',
    [System.Text.RegularExpressions.RegexOptions]::Singleline
)

if (-not $statePatternMatch.Success) {
    throw 'GUI STATE_PATTERN was not found in gui/main.py.'
}

$sampleStateLine = '[State] STATE:RECOVERY_WAIT'
$sampleStateMatch = [regex]::Match($sampleStateLine, $statePatternMatch.Groups['pattern'].Value)

if (-not $sampleStateMatch.Success -or $sampleStateMatch.Groups.Count -ne 2) {
    throw "GUI STATE_PATTERN does not parse the expected State line: $sampleStateLine"
}

$expectedFirmwareFormat = '[Measure] V1:%0.2fV V2:%0.2fV CO:%0.2fA VO:%0.2fV T:%0.1fC Vref:%0.3fV'
if (-not $firmware.Contains($expectedFirmwareFormat)) {
    throw "Firmware UART output does not contain GUI-compatible format: $expectedFirmwareFormat"
}

$formatIndex = $firmware.IndexOf($expectedFirmwareFormat)
$snprintfSnippet = $firmware.Substring($formatIndex, [Math]::Min(1200, $firmware.Length - $formatIndex))

$expectedArgumentOrder = @(
    'Measure_GetV1In()',
    'Measure_GetV2In()',
    'Measure_GetCoOut()',
    'Measure_GetVoOut()',
    'Measure_GetTemp()',
    'Measure_GetVref()'
)

$lastIndex = -1
foreach ($argument in $expectedArgumentOrder) {
    $currentIndex = $snprintfSnippet.IndexOf($argument)
    if ($currentIndex -lt 0) {
        throw "Firmware UART Measure format is missing argument: $argument"
    }
    if ($currentIndex -le $lastIndex) {
        throw "Firmware UART Measure arguments are not in GUI field order near: $argument"
    }
    $lastIndex = $currentIndex
}

$txItemMaxMatch = [regex]::Match($firmware, 'sizeof\(measure_buf\)')
if (-not $txItemMaxMatch.Success) {
    throw 'Firmware Measure telemetry should be formatted in its own UART DMA frame.'
}

if (-not $firmware.Contains('sizeof(code_buf)')) {
    throw 'Firmware raw ADC code diagnostics should be formatted in a separate UART DMA frame.'
}

$expectedCodeFormat = '[Code] 0:%u,1:%u,2:%u,3:%u,4:%u\r\n'
if (-not $firmware.Contains($expectedCodeFormat)) {
    throw "Firmware raw ADC diagnostics do not use the 5-channel KB format: $expectedCodeFormat"
}

if ($firmware.Contains('Measure_GetRawCode(5)')) {
    throw 'Firmware still reads a sixth ADC raw code that should have been removed with VREFINT.'
}

if (-not $firmware.Contains('Measure_GetRawCode(4)')) {
    throw 'Firmware raw ADC diagnostics are missing the temperature channel raw code.'
}

foreach ($needle in @(
    'strstr(cmd_buf, "SetDAC:")',
    'strtof(p_val, &endptr)',
    'Output_Control_Status_t control_status',
    'control_status = Output_Control_SetCurrent(target_val)',
    'if (control_status != OUTPUT_CONTROL_OK)',
    'Calc_Control_IsClosedLoopActive()',
    'Measure_IsReady() && (Output_Protection_GetState() == OUTPUT_PROTECTION_NORMAL)',
    'if (controls_safe != 0U)',
    'strstr(cmd_buf, "SetOF:")',
    'control_status = Output_Control_Enable()',
    'Output_Control_Disable()',
    'Output_Control_IsEnabled()',
    'strstr(cmd_buf, "SystemReset")',
    'NVIC_SystemReset();',
    'OF_ENABLED',
    'OF_DISABLED',
    '[Error] OPEN_CIRCUIT_DETECTED',
    '[Stable] Count:%u/%u Delta:%0.1fmA Wait:%ums',
    '[DebugIOC] dU:%0.2fV dI:%0.2fA Rraw:%0.3fR Vac:%0.2fV VoutAvg:%0.2fV Pin:%0.1fW Pout:%0.1fW IOC:%0.2fA',
    'strstr(cmd_buf, "DebugPause2A")',
    'Calc_Control_PauseWait2A',
    'strstr(cmd_buf, "DebugResume2A")',
    'Calc_Control_ResumeWait2A',
    'Calc_Control_IsWait2APaused',
    'WAIT_2A_PAUSED'
)) {
    if (-not $firmware.Contains($needle)) {
        throw "Firmware UART control path is missing expected implementation: $needle"
    }
}

foreach ($needle in @(
    'static volatile uint8_t s_pause_state_ack_pending = 0U;',
    'static volatile uint8_t s_resume_state_ack_pending = 0U;',
    's_pause_state_ack_pending = 1U;',
    's_resume_state_ack_pending = 1U;',
    's_pause_state_ack_pending = 0U;',
    's_resume_state_ack_pending = 0U;'
)) {
    if (-not $firmware.Contains($needle)) {
        throw "Calc state observer dedupe is missing expected ack-suppression hook: $needle"
    }
}

$pauseDedupePattern = 'if\s*\(\s*\(current_state == CALC_CONTROL_WAIT_2A_STABLE\)\s*&&\s*\(wait2a_paused != 0U\)\s*\)[\s\S]*?s_pause_state_ack_pending'
if (-not [regex]::IsMatch($firmware, $pauseDedupePattern)) {
    throw 'Paused-state publisher must consume the command-ack suppression flag before posting a duplicate WAIT_2A_PAUSED frame.'
}

$resumeDedupePattern = 'else if\s*\(current_state != last_state\)[\s\S]*?s_resume_state_ack_pending'
if (-not [regex]::IsMatch($firmware, $resumeDedupePattern)) {
    throw 'Resumed WAIT_2A_STABLE publisher must consume the command-ack suppression flag before posting a duplicate WAIT_2A_STABLE_10S frame.'
}

$resumeOrderPattern = 'else if\s*\(current_state != last_state\)\s*\{[\s\S]*?if\s*\(\s*\(current_state == CALC_CONTROL_WAIT_2A_STABLE\)\s*&&\s*\(s_resume_state_ack_pending != 0U\)\s*\)[\s\S]*?last_state = current_state;'
if (-not [regex]::IsMatch($firmware, $resumeOrderPattern)) {
    throw 'Resumed WAIT_2A_STABLE publisher must clear the ack flag before caching current_state.'
}

foreach ($needle in @(
    'taskENTER_CRITICAL();',
    'taskEXIT_CRITICAL();'
)) {
    if (-not $calcControl.Contains($needle)) {
        throw "Calc control pause/resume paths must use critical sections: $needle"
    }
}

$pauseCriticalPattern = 'uint8_t Calc_Control_PauseWait2A\(uint32_t tick_ms\)[\s\S]*?taskENTER_CRITICAL\(\);[\s\S]*?s_wait2a_paused = 1U;[\s\S]*?taskEXIT_CRITICAL\(\);'
if (-not [regex]::IsMatch($calcControl, $pauseCriticalPattern)) {
    throw 'Calc_Control_PauseWait2A must lock its shared state with a critical section.'
}

$resumeCriticalPattern = 'uint8_t Calc_Control_ResumeWait2A\(uint32_t tick_ms\)[\s\S]*?taskENTER_CRITICAL\(\);[\s\S]*?s_state_start_tick \+= pause_elapsed;[\s\S]*?taskEXIT_CRITICAL\(\);'
if (-not [regex]::IsMatch($calcControl, $resumeCriticalPattern)) {
    throw 'Calc_Control_ResumeWait2A must lock its shared state with a critical section.'
}

$wait2aCriticalPattern = 'case CALC_CONTROL_WAIT_2A_STABLE:[\s\S]*?taskENTER_CRITICAL\(\);[\s\S]*?s_wait2a_paused[\s\S]*?taskEXIT_CRITICAL\(\);'
if (-not [regex]::IsMatch($calcControl, $wait2aCriticalPattern)) {
    throw 'WAIT_2A_STABLE evaluation must snapshot pause state under a critical section.'
}

foreach ($needle in @(
    'static void PostCalcStateLine(const char *state_str)',
    'PostCalcStateLine("SET_3A")',
    'PostCalcStateLine("LATCH_3A")',
    'PostCalcStateLine("SET_2A")',
    'PostCalcStateLine("LATCH_2A")',
    'PostCalcStateLine("CALC_RESISTANCE")'
)) {
    if (-not $firmware.Contains($needle)) {
        throw "Calc transient state publication is missing expected explicit observer hook: $needle"
    }
}

$stableBeforeTransientPattern = '\[Stable\] Count:%u/%u Delta:%0\.1fmA Wait:%ums[\s\S]*?PostCalcStateLine\("LATCH_3A"\)'
if (-not [regex]::IsMatch($firmware, $stableBeforeTransientPattern)) {
    throw 'Transient 3A completion states must be emitted after the Stable diagnostic block so Count:5/5 appears before LATCH_3A.'
}

foreach ($needle in @(
    'self.system_group = QGroupBox("系统控制", central_widget)',
    'self.diag_group = QGroupBox("状态与内阻诊断", central_widget)',
    'self.btn_system_reset.clicked.connect(self.send_system_reset)',
    'self.btn_pause2a',
    'self.toggle_pause2a',
    'self.update_pause2a_button_state',
    'WAIT_2A_PAUSED',
    'DebugPause2A',
    'DebugResume2A',
    'self.btn_pause2a.setEnabled(False)'
)) {
    if (-not $gui.Contains($needle)) {
        throw "GUI control path is missing expected implementation: $needle"
    }
}

foreach ($needle in @(
    'SystemReset\r\n',
    '[Stable] Count:<stable_count>/<stable_target> Delta:<delta_mA>mA Wait:<elapsed_ms>ms',
    '[Error] OPEN_CIRCUIT_DETECTED',
    'DebugPause2A',
    'DebugResume2A',
    '[State] STATE:WAIT_2A_PAUSED',
    '[State] STATE:PAUSE_FAILED',
    '[State] STATE:RESUME_FAILED'
)) {
    if (-not $interface.Contains($needle)) {
        throw "Interface standard is missing expected command or diagnostic line: $needle"
    }
}

$dacControlPath = Join-Path $repoRoot 'Core/Src/dac_control.c'
$dacControl = Get-Content -Raw -LiteralPath $dacControlPath
foreach ($needle in @(
    'if (current_A < 0.0f)',
    'if (current_A > 15.0f)',
    'current_A = 15.0f;',
    'DAC_Control_SetValue(val_u);'
)) {
    if (-not $dacControl.Contains($needle)) {
        throw "DAC control module is missing expected safety behavior: $needle"
    }
}

$outputControlPath = Join-Path $repoRoot 'Core/Src/output_control.c'
$outputControl = Get-Content -Raw -LiteralPath $outputControlPath
$outputControlHeaderPath = Join-Path $repoRoot 'Core/Inc/output_control.h'
$outputControlHeader = Get-Content -Raw -LiteralPath $outputControlHeaderPath
foreach ($needle in @(
    'typedef enum',
    'OUTPUT_CONTROL_OK = 0',
    'OUTPUT_CONTROL_REJECTED = 1',
    'Output_Control_Status_t',
    'Output_Control_Status_t Output_Control_Enable(void)',
    'void Output_Control_Disable(void)',
    'Output_Control_Status_t Output_Control_SetCurrent(float current_A)',
    'void Output_Control_Disable(void)',
    'void Output_Control_ClearFaultOutput(void)',
    'uint8_t Output_Control_IsEnabled(void)'
)) {
    if (-not $outputControlHeader.Contains($needle)) {
        throw "Output control header is missing expected status API: $needle"
    }
}

foreach ($needle in @(
    'static volatile uint8_t s_output_enabled = 0U;',
    'void Output_Control_Init(void)',
    'Output_Control_Status_t Output_Control_Enable(void)',
    'Output_Control_Status_t Output_Control_SetCurrent(float current_A)',
    'void Output_Control_ClearFaultOutput(void)',
    'uint8_t Output_Control_IsEnabled(void)',
    'taskENTER_CRITICAL();',
    'taskEXIT_CRITICAL();',
    'HAL_GPIO_WritePin(OF_EN_GPIO_Port, OF_EN_Pin, GPIO_PIN_RESET)',
    'HAL_GPIO_WritePin(OF_EN_GPIO_Port, OF_EN_Pin, GPIO_PIN_SET)',
    's_output_enabled = 1U;',
    's_output_enabled = 0U;',
    'DAC_Control_UpdatePfcTargetCurrent(0.0f)',
    'DAC_Control_SetPfcCurrent(0.0f)',
    '!(current_A >= 0.0f && current_A <= 15.0f)',
    'Output_Protection_GetState() != OUTPUT_PROTECTION_NORMAL',
    'current_A != 0.0f',
    'return OUTPUT_CONTROL_REJECTED;',
    'return OUTPUT_CONTROL_OK;'
)) {
    if (-not $outputControl.Contains($needle)) {
        throw "Output control module is missing expected implementation: $needle"
    }
}

$enableGatePattern = 'Output_Control_Status_t\s+Output_Control_Enable\s*\(\s*void\s*\)\s*\{\s*taskENTER_CRITICAL\(\);\s*if\s*\(\s*Output_Protection_GetState\(\)\s*!=\s*OUTPUT_PROTECTION_NORMAL\s*\)\s*\{\s*taskEXIT_CRITICAL\(\);\s*return\s+OUTPUT_CONTROL_REJECTED;'
if (-not [regex]::IsMatch($outputControl, $enableGatePattern, [System.Text.RegularExpressions.RegexOptions]::Singleline)) {
    throw 'Output_Control_Enable() must reject and report enable requests when protection is not NORMAL.'
}

$setCurrentRejectNeedles = @(
    'Output_Control_Status_t Output_Control_SetCurrent(float current_A)',
    'if ((Output_Protection_GetState() != OUTPUT_PROTECTION_NORMAL) && (current_A != 0.0f))',
    'return OUTPUT_CONTROL_REJECTED;'
)
foreach ($needle in $setCurrentRejectNeedles) {
    if (-not $outputControl.Contains($needle)) {
        throw "Output_Control_SetCurrent() rejection path is missing: $needle"
    }
}

if (-not [regex]::IsMatch($outputControl, 'taskENTER_CRITICAL\(\);\s*if\s*\(\(Output_Protection_GetState\(\) != OUTPUT_PROTECTION_NORMAL\)', [System.Text.RegularExpressions.RegexOptions]::Singleline)) {
    throw 'Output_Control_Enable() must make the protection check and GPIO write a critical check-then-act region.'
}

if (-not [regex]::IsMatch($outputControl, 'taskENTER_CRITICAL\(\);.*Output_Protection_GetState\(\) != OUTPUT_PROTECTION_NORMAL.*DAC_Control_SetPfcCurrent', [System.Text.RegularExpressions.RegexOptions]::Singleline)) {
    throw 'Output_Control_SetCurrent() must reject and report non-zero current requests when protection is not NORMAL.'
}

foreach ($pattern in @(
    '^[ \t]*Output_Control_SetCurrent\s*\(\s*target_val\s*\)\s*;',
    '^[ \t]*Output_Control_SetCurrent\s*\(\s*calc_out\.set_current_a\s*\)\s*;',
    '^[ \t]*Output_Control_Enable\s*\(\s*\)\s*;'
)) {
    if ([regex]::IsMatch($firmware, $pattern, [System.Text.RegularExpressions.RegexOptions]::Multiline)) {
        throw "Output control return value must not be silently ignored. Pattern: $pattern"
    }
}

foreach ($needle in @(
    'control_status = Output_Control_SetCurrent(calc_out.set_current_a)',
    'control_status = Output_Control_Enable()'
)) {
    if (-not $firmware.Contains($needle)) {
        throw "Firmware must consume output control result: $needle"
    }
}

if ($firmware.Contains("HAL_GPIO_WritePin(OF_EN_GPIO_Port")) {
    throw 'Firmware app_freertos.c should not write OF_EN pin directly. Use Output_Control APIs instead.'
}

# 静态校验任务与消息队列的创建
$needles = 'emergencyTaskHandle', 'calcControlTaskHandle', 'uartMsgQueueHandle', 'osPriorityRealtime', 'osMessageQueueNew', 'osMessageQueuePut', 'osMessageQueueGet'
foreach ($needle in $needles) {
    if (-not $firmware.Contains($needle)) {
        throw "Firmware app_freertos.c is missing multithreading integration: $needle"
    }
}

if ($firmware.Contains('Output_Control_SetCurrent(DAC_Control_GetPfcTargetCurrent())')) {
    throw 'Sample_Filter_Task must not periodically re-drive the DAC target; SetDAC and Calc_Control_Task own Output_Control_SetCurrent().'
}

if ($firmware.Contains('DAC_Control_UpdatePfcTargetCurrent(target_val)')) {
    throw 'SetDAC command must cross the Output_Control boundary instead of updating the DAC target directly.'
}

if ($firmware.Contains('Run_State_Machines_TDD_Test')) {
    throw 'Production firmware must not execute in-band state machine self-tests. Keep state machine verification in tests/.'
}

if ($firmware.Contains('g_calc_reset_req')) {
    throw 'Calc reset must use an RTOS task notification/thread flag, not a shared global polling flag.'
}

if ($firmware.Contains('Calc_Control_Reset();')) {
    throw 'Emergency_Task must notify Calc_Control_Task instead of directly resetting the calc state machine.'
}

foreach ($needle in @(
    'Output_Control_Init();',
    'CALC_CONTROL_RESET_FLAG',
    'calcControlTaskHandle != NULL',
    'osThreadFlagsSet(calcControlTaskHandle, CALC_CONTROL_RESET_FLAG)',
    'osThreadFlagsClear(CALC_CONTROL_RESET_FLAG)'
)) {
    if (-not $firmware.Contains($needle)) {
        throw "Firmware Emergency_Task/Calc_Control_Task reset handoff is missing: $needle"
    }
}

foreach ($needle in @(
    'if ((reset_flags & osFlagsError) != 0U)',
    'reset_request = 1U;',
    'calc_in.reset_request = reset_request;'
)) {
    if (-not $firmware.Contains($needle)) {
        throw "Calc_Control_Task must handle osThreadFlagsClear() error returns explicitly: $needle"
    }
}

$uartTxCalls = [regex]::Matches($firmware, 'UartDma_Transmit_NonBlocking\(').Count
if ($uartTxCalls -ne 1) {
    throw "UART DMA transmit must have exactly one app_freertos.c exit point in StartUartTask queue consumer. Found $uartTxCalls calls."
}

$outputProtectionPath = Join-Path $repoRoot 'Core/Src/output_protection.c'
$outputProtection = Get-Content -Raw -LiteralPath $outputProtectionPath
foreach ($needle in @(
    'OTP_TRIP_THRESHOLD',
    'OVP_TRIP_THRESHOLD',
    'OCP_TRIP_THRESHOLD',
    '!is_safe_limit',
    'RECOVERY_OBSERVE_MS',
    'OUTPUT_PROTECTION_RECOVERY_WAIT'
)) {
    if (-not $outputProtection.Contains($needle)) {
        throw "Output protection state machine is missing expected threshold/state logic: $needle"
    }
}
if (-not $outputProtection.Contains('static volatile Output_Protection_State_t s_state')) {
    throw 'Output protection state must be volatile because it is published across RTOS tasks.'
}

foreach ($needle in @(
    'STEP_3A_STABLE_MS',
    'STEP_2A_STABLE_MS',
    'CALC_CONTROL_CALC_RESISTANCE',
    'float di = s_i1 - s_i2;',
    'if (di > 0.01f)',
    'CALC_CONTROL_IOC_GAIN_K',
    'CALC_CONTROL_IOC_MIN_A',
    'CALC_CONTROL_IOC_MAX_A',
    'static float s_ioc_target_a = 0.0f;',
    'static uint8_t s_ioc_valid = 0U;',
    'Calc_Control_IsClosedLoopActive',
    'resistance_candidate = fabsf(s_u1 - s_u2) / di;',
    'if (resistance_candidate >= 0.0f)',
    'CALC_CONTROL_MONITOR',
    'static uint8_t s_wait2a_paused',
    's_wait2a_paused = 1U',
    's_wait2a_paused = 0U',
    'Calc_Control_PauseWait2A',
    'Calc_Control_ResumeWait2A',
    'Calc_Control_IsWait2APaused'
)) {
    if (-not $calcControl.Contains($needle)) {
        throw "Calc control state machine is missing expected timing/calculation logic: $needle"
    }
}

$closedLoopRejectPattern = 'char \*p_val = strstr\(cmd_buf, "SetDAC:"\);[\s\S]*?Calc_Control_IsClosedLoopActive\(\)[\s\S]*?UartQueue_PostBytes\(MSG_TYPE_STATE, rejected_msg'
if (-not [regex]::IsMatch($firmware, $closedLoopRejectPattern)) {
    throw 'SetDAC path must reject manual current commands once the calc closed-loop IOC target is active.'
}

foreach ($needle in @(
    'static uint8_t s_open_circuit_latched = 0U;',
    's_open_circuit_latched = 0U;',
    's_open_circuit_latched = 1U;'
)) {
    if (-not $calcControl.Contains($needle)) {
        throw "Calc control state machine is missing expected open-circuit latch behavior: $needle"
    }
}

$openCircuitGatePattern = 'if\s*\(\s*input->safe_allowed\s*&&\s*\(s_open_circuit_latched\s*==\s*0U\)\s*\)'
if (-not [regex]::IsMatch($calcControl, $openCircuitGatePattern)) {
    throw 'Calc control WAIT_SAFE must block auto-restart while the open-circuit latch is set.'
}

foreach ($needle in @(
    'while (keep_running != 0U)',
    'keep_running = 1U;',
    'CALC_CONTROL_SET_3A',
    'CALC_CONTROL_LATCH_3A',
    'CALC_CONTROL_SET_2A',
    'CALC_CONTROL_LATCH_2A'
)) {
    if (-not $calcControl.Contains($needle)) {
        throw "Calc control state machine must advance immediate states inside one update call: $needle"
    }
}

if (Test-Path -LiteralPath (Join-Path $repoRoot 'PSA')) {
    throw 'Nested PSA/ subproject must not exist inside the FreeRTOS_PSA root project.'
}

$maxMeasureLine = "`r`n[Measure] V1:999.99V V2:999.99V CO:-99.99A VO:999.99V T:-273.1C Vref:9.999V"
$measureLineBytes = [System.Text.Encoding]::ASCII.GetByteCount($maxMeasureLine)

if ($measureLineBytes -gt 128) {
    throw "Expected Measure telemetry frame is longer than UART_DMA_TX_ITEM_MAX_LEN: $measureLineBytes bytes"
}

$adcDmaDriverPath = Join-Path $repoRoot 'Core/Src/adc_dma_driver.c'
$adcDmaDriver = Get-Content -Raw -LiteralPath $adcDmaDriverPath
if (-not $adcDmaDriver.Contains('static volatile bool       g_driver_ready = false;')) {
    throw 'ADC driver readiness flag must be volatile because multiple RTOS tasks read it while SampleFilterTask updates it.'
}
if ($adcDmaDriver.Contains('void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef* hadc)') -and
    [regex]::IsMatch($adcDmaDriver, 'void HAL_ADC_ConvHalfCpltCallback\([^)]*\)\s*\{\s*if\s*\([^)]*\)\s*\{\s*Measure_Update\(\);')) {
    throw 'ADC ConvHalfCpltCallback must not call Measure_Update() directly to prevent ISR re-entrancy.'
}
if ($adcDmaDriver.Contains('void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef* hadc)') -and
    [regex]::IsMatch($adcDmaDriver, 'void HAL_ADC_ConvCpltCallback\([^)]*\)\s*\{\s*if\s*\([^)]*\)\s*\{\s*Measure_Update\(\);')) {
    throw 'ADC ConvCpltCallback must not call Measure_Update() directly to prevent ISR re-entrancy.'
}

$freeRtosConfigPath = Join-Path $repoRoot 'Core/Inc/FreeRTOSConfig.h'
$freeRtosConfig = Get-Content -Raw -LiteralPath $freeRtosConfigPath
if (-not $freeRtosConfig.Contains('#define configCHECK_FOR_STACK_OVERFLOW 2')) {
    throw 'FreeRTOSConfig.h must define configCHECK_FOR_STACK_OVERFLOW as 2 for robust runtime checking.'
}

$firmware = Get-Content -Raw -LiteralPath $firmwarePath
if ($firmware.Contains('128 * 4') -or $firmware.Contains('256 * 4')) {
    throw 'Firmware task stacks are too tight; all formatting/state tasks must use at least 512 * 4 bytes.'
}
if (-not $firmware.Contains('vApplicationStackOverflowHook')) {
    throw 'Firmware is missing implementation of vApplicationStackOverflowHook.'
}

if (-not $firmware.Contains('s_backlog_msg') -or -not $firmware.Contains('s_backlog_valid')) {
    throw 'Firmware StartUartTask must declare s_backlog_msg and s_backlog_valid for reliable queue pop.'
}
if (-not $firmware.Contains('UartDma_Transmit_NonBlocking(&g_uart1_dma, s_backlog_msg.payload, s_backlog_msg.len) == HAL_OK')) {
    throw 'Firmware must check UartDma_Transmit_NonBlocking() return value against HAL_OK to retry on HAL_BUSY.'
}
foreach ($needle in @(
    '#define UART_BACKLOG_RETRY_MAX',
    '#define UART_BACKLOG_BACKOFF_MS',
    'static uint8_t s_backlog_retry_count = 0U;',
    's_backlog_retry_count++',
    'if (s_backlog_retry_count >= UART_BACKLOG_RETRY_MAX)',
    's_backlog_retry_count = 0U;',
    'osDelay(UART_BACKLOG_BACKOFF_MS);'
)) {
    if (-not $firmware.Contains($needle)) {
        throw "Firmware StartUartTask must bound UART backlog retries with backoff/drop behavior: $needle"
    }
}

$stackHookStart = $firmware.IndexOf('void vApplicationStackOverflowHook')
if ($stackHookStart -lt 0) {
    throw 'Firmware is missing implementation of vApplicationStackOverflowHook.'
}
$stackHookBody = $firmware.Substring($stackHookStart)
if ($stackHookBody.Contains('Output_Control_Disable()') -or $stackHookBody.Contains('HAL_')) {
    throw 'Stack overflow hook must not call Output_Control or HAL functions when task stack is already suspect.'
}
foreach ($needle in @(
    'portDISABLE_INTERRUPTS();',
    'OF_EN_GPIO_Port->BRR = (uint32_t)OF_EN_Pin;',
    'DAC1->DHR12R1 = 0U;'
)) {
    if (-not $stackHookBody.Contains($needle)) {
        throw "Stack overflow hook must force output safe state with minimal direct register writes: $needle"
    }
}
foreach ($needle in @(
    'type == MSG_TYPE_STATE',
    'osMessageQueueGet(uartMsgQueueHandle, &dropped_msg, NULL, 0U)',
    'if (put_status != osOK)',
    'UartQueue_PostBytes(MSG_TYPE_STATE, state_msg.payload, state_msg.len)',
    'UartQueue_PostBytes(MSG_TYPE_CALC, report_msg.payload, report_msg.len)'
)) {
    if (-not $firmware.Contains($needle)) {
        throw "Firmware UartQueue_PostBytes() is missing priority-aware queue-full handling: $needle"
    }
}

Write-Host 'UART GUI protocol check passed.'
