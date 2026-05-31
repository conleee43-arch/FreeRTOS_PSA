$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot

function Assert-Contains {
    param(
        [string]$Path,
        [string]$Needle,
        [string]$Message
    )

    $content = Get-Content -LiteralPath (Join-Path $root $Path) -Raw
    if (-not $content.Contains($Needle)) {
        throw $Message
    }
}

Assert-Contains 'Core\Src\stm32g4xx_hal_msp.c' `
    'HAL_SYSCFG_VREFBUF_VoltageScalingConfig(SYSCFG_VREFBUF_VOLTAGE_SCALE1);' `
    'VREFBUF voltage scale is not configured.'

Assert-Contains 'Core\Src\stm32g4xx_hal_msp.c' `
    'HAL_SYSCFG_VREFBUF_HighImpedanceConfig(SYSCFG_VREFBUF_HIGH_IMPEDANCE_DISABLE);' `
    'VREF+ pin is not connected to VREFBUF output.'

Assert-Contains 'Core\Src\stm32g4xx_hal_msp.c' `
    'HAL_SYSCFG_EnableVREFBUF();' `
    'VREFBUF is not enabled.'

Assert-Contains 'FreeRTOS_PSA.ioc' `
    'SYS.VoltageScaling=SYSCFG_VREFBUF_VOLTAGE_SCALE1' `
    'CubeMX project does not preserve VREFBUF voltage scale.'

Assert-Contains 'FreeRTOS_PSA.ioc' `
    'VREF+.Signal=VREFBUF_OUT' `
    'CubeMX project does not preserve VREFBUF output on VREF+.'

Assert-Contains 'Core\Src\adc_dma_driver.c' `
    '#define ADC_DRV_ENABLE_HW_CALIB     1U' `
    'ADC hardware calibration is disabled.'

Assert-Contains 'Core\Inc\adc_calib.h' `
    '#define ADC_CALIB_TS_CAL1_ADDR      (0x1FFF75A8UL)' `
    'STM32G4 TS_CAL1 address does not match STM32G4 LL header.'

Assert-Contains 'Core\Inc\adc_calib.h' `
    '#define ADC_CALIB_TS_CAL2_ADDR      (0x1FFF75CAUL)' `
    'STM32G4 TS_CAL2 address does not match STM32G4 LL header.'

Write-Host 'ADC configuration checks passed.'
