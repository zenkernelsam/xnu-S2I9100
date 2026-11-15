#!/usr/bin/env python3
"""UART0 fractional baud/RTS-CTS and PWM1 safe carrier register models."""
import argparse
from i9100_offline_build import Evidence
p=argparse.ArgumentParser(description=__doc__)
p.add_argument("--cross-compile",action="store_true")
a=p.parse_args()
sources=["i9100_uart_bluetooth.c","i9100_pwm_haptic.c"]
e=Evidence("uart-pwm-offline")
e.model("i9100_uart_pwm_test.c",sources)
if a.cross_compile:e.cross_compile(sources)
e.finish(sources,"UART0 and Exynos PWM1 register/clock/fault models; no BT or motor hardware test")
