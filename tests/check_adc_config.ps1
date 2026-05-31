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

function Assert-Matches {
    param(
        [string]$Path,
        [string]$Pattern,
        [string]$Message
    )

    $content = Get-Content -LiteralPath (Join-Path $root $Path) -Raw
    if (-not [regex]::IsMatch($content, $Pattern, [System.Text.RegularExpressions.RegexOptions]::Multiline)) {
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

Assert-Matches 'FreeRTOS_PSA.ioc' `
    '^Mcu\.IP\d+=DAC1$' `
    'CubeMX project does not preserve DAC1 as a configured peripheral.'

Assert-Contains 'FreeRTOS_PSA.ioc' `
    'PA4.Signal=COMP_DAC11_group' `
    'CubeMX project does not preserve PA4 as DAC1_OUT1.'

Assert-Contains 'FreeRTOS_PSA.ioc' `
    'DAC1.DAC_Channel-DAC_OUT1=DAC_CHANNEL_1' `
    'CubeMX project does not preserve DAC1 channel 1 output.'

Assert-Contains 'FreeRTOS_PSA.ioc' `
    'ProjectManager.functionlistsort=1-SystemClock_Config-RCC-false-HAL-false,2-MX_GPIO_Init-GPIO-false-HAL-true,3-MX_USART1_UART_Init-USART1-false-HAL-true,4-MX_ADC1_Init-ADC1-false-HAL-true,5-MX_DAC1_Init-DAC1-false-HAL-true' `
    'CubeMX project does not preserve ADC1 and DAC1 init ordering.'

Assert-Contains 'Core\Inc\stm32g4xx_hal_conf.h' `
    '#define HAL_DAC_MODULE_ENABLED' `
    'HAL DAC module is not enabled.'

Assert-Contains 'Core\Inc\main.h' `
    '#define IOC_Pin        GPIO_PIN_4' `
    'IOC PA4 pin define is missing.'

Assert-Contains 'Core\Inc\main.h' `
    '#define OF_EN_Pin      GPIO_PIN_0' `
    'OF_EN PB0 pin define is missing.'

Assert-Contains 'Core\Src\main.c' `
    'MX_DAC1_Init();' `
    'DAC1 is not initialized from main.c.'

Assert-Contains 'MDK-ARM\FreeRTOS_PSA.uvprojx' `
    '<FilePath>../Core/Src/dac_control.c</FilePath>' `
    'Keil project does not include dac_control.c.'

Assert-Contains 'MDK-ARM\FreeRTOS_PSA.uvprojx' `
    '<FilePath>../Core/Src/dac.c</FilePath>' `
    'Keil project does not include dac.c.'

Assert-Contains 'MDK-ARM\FreeRTOS_PSA.uvprojx' `
    '<FilePath>../Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_dac.c</FilePath>' `
    'Keil project does not include the HAL DAC source.'

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
