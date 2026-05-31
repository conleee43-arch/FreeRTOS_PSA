$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$firmwarePath = Join-Path $repoRoot 'Core/Src/app_freertos.c'
$guiPath = Join-Path $repoRoot 'gui/main.py'

$firmware = Get-Content -Raw -LiteralPath $firmwarePath
$gui = Get-Content -Raw -LiteralPath $guiPath

$patternMatch = [regex]::Match(
    $gui,
    'MEASURE_PATTERN\s*=\s*re\.compile\(\s*r"(?<pattern>[^"]+)"\s*\)',
    [System.Text.RegularExpressions.RegexOptions]::Singleline
)

if (-not $patternMatch.Success) {
    throw 'GUI MEASURE_PATTERN was not found in gui/main.py.'
}

$guiPattern = $patternMatch.Groups['pattern'].Value
$sampleMeasureLine = '[Measure] V1:223.90V V2:223.90V CO:10.80A VO:360.00V T:25.0C Vref:1.800V'
$sampleMatch = [regex]::Match($sampleMeasureLine, $guiPattern)

if (-not $sampleMatch.Success -or $sampleMatch.Groups.Count -ne 7) {
    throw "GUI MEASURE_PATTERN does not parse the expected Measure line: $sampleMeasureLine"
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

foreach ($needle in @(
    'strstr(cmd_buf, "SetDAC:")',
    'strtof(p_val, &endptr)',
    'DAC_Control_UpdatePfcTargetCurrent(target_val)',
    'DAC_Control_SetPfcCurrent(DAC_Control_GetPfcTargetCurrent())',
    'strstr(cmd_buf, "SetOF:")',
    'HAL_GPIO_WritePin(OF_EN_GPIO_Port, OF_EN_Pin, GPIO_PIN_SET)',
    'HAL_GPIO_WritePin(OF_EN_GPIO_Port, OF_EN_Pin, GPIO_PIN_RESET)'
)) {
    if (-not $firmware.Contains($needle)) {
        throw "Firmware UART control path is missing expected implementation: $needle"
    }
}

foreach ($needle in @(
    'cmd = f"SetDAC:{current_val:.2f}\r\n"',
    'cmd = f"SetOF:{state}\r\n"',
    'self.dac_spin.setRange(0.00, 10.00)',
    'self.btn_of_en_on.clicked.connect(lambda: self.transmit_of_en_setting(1))',
    'self.btn_of_en_off.clicked.connect(lambda: self.transmit_of_en_setting(0))'
)) {
    if (-not $gui.Contains($needle)) {
        throw "GUI control path is missing expected implementation: $needle"
    }
}

$dacControlPath = Join-Path $repoRoot 'Core/Src/dac_control.c'
$dacControl = Get-Content -Raw -LiteralPath $dacControlPath
foreach ($needle in @(
    'if (current_A < 0.0f)',
    'if (current_A > 10.0f)',
    'current_A = 10.0f;',
    'DAC_Control_SetValue(val_u);'
)) {
    if (-not $dacControl.Contains($needle)) {
        throw "DAC control module is missing expected safety behavior: $needle"
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

Write-Host 'UART GUI protocol check passed.'
