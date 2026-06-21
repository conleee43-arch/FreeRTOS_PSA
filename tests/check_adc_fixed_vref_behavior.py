# -*- coding: utf-8 -*-
from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[1]
ADC_CALIB_H = ROOT / "Core" / "Inc" / "adc_calib.h"
ADC_CALIB_C = ROOT / "Core" / "Src" / "adc_calib.c"
ADC_DMA_DRIVER_C = ROOT / "Core" / "Src" / "adc_dma_driver.c"


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def assert_contains(content: str, needle: str, label: str) -> None:
    if needle not in content:
        raise AssertionError(f"{label}: missing {needle!r}")


def assert_not_contains(content: str, needle: str, label: str) -> None:
    if needle in content:
        raise AssertionError(f"{label}: unexpected {needle!r}")


def assert_matches(content: str, pattern: str, label: str) -> None:
    if re.search(pattern, content, re.MULTILINE) is None:
        raise AssertionError(f"{label}: pattern not found {pattern!r}")


def main() -> int:
    adc_calib_h = read_text(ADC_CALIB_H)
    adc_calib_c = read_text(ADC_CALIB_C)
    adc_dma_driver_c = read_text(ADC_DMA_DRIVER_C)

    assert_contains(
        adc_calib_h,
        "#define ADC_CALIB_FIXED_VREF_V",
        "fixed external VREF constant is not declared",
    )
    assert_contains(
        adc_calib_h,
        "(2.5f)",
        "fixed external VREF constant is not pinned to 2.5V",
    )
    assert_contains(
        adc_calib_c,
        "gs_calib_data.vref_inst = ADC_CALIB_FIXED_VREF_V;",
        "instantaneous VREF is not pinned to the fixed 2.5V reference",
    )
    assert_contains(
        adc_calib_c,
        "gs_calib_data.vref_ema  = ADC_CALIB_FIXED_VREF_V;",
        "EMA VREF is not pinned to the fixed 2.5V reference",
    )
    assert_not_contains(
        adc_calib_c,
        "vref_new = ADC_CALIB_VREFINT_CAL_V * ((float)vrefint_cal / (float)vrefint_raw);",
        "legacy VREFINT-based dynamic reference calculation is still active",
    )
    assert_contains(
        adc_calib_c,
        "ts_data_cal = (float)temp_raw * (gs_calib_data.vref_ema / ADC_CALIB_TS_CAL_V);",
        "temperature conversion no longer scales back to the factory calibration voltage",
    )
    assert_not_contains(
        adc_dma_driver_c,
        "const float vrefint_raw = g_filters[5].filtered_val;",
        "ADC DMA driver still consumes the legacy VREFINT filter slot",
    )
    assert_matches(
        adc_dma_driver_c,
        r"ADC_Calib_Update\(\s*0U\s*,\s*\(uint16_t\)ts_raw\s*\)",
        "ADC DMA driver does not call ADC_Calib_Update with the fixed-reference path",
    )

    print("ADC fixed VREF behavior checks passed.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as exc:
        print(str(exc), file=sys.stderr)
        raise SystemExit(1)
