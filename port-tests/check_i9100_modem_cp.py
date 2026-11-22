#!/usr/bin/env python3
"""Offline EUR U1 XMM6260 CP control-state checks without any modem IO."""
import argparse
from i9100_offline_build import Evidence
p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--cross-compile", action="store_true")
a = p.parse_args()
e = Evidence("modem-cp-offline")
e.model("i9100_modem_cp_test.c",
        ["i9100_gpio_broker.c", "i9100_peripheral_board.c", "i9100_modem_cp.c"])
e.model_cpp("i9100_modem_cp_native_test.cpp",
            ["i9100_gpio_broker.c", "i9100_clock_gates.c", "i9100_irq_registry.c",
             "i9100_peripheral_board.c", "i9100_i2c_samsung.c", "i9100_uart_bluetooth.c",
             "i9100_sdio.c", "i9100_irq_cascade.c", "i9100_modem_cp.c"],
            ["i9100_modem_cp_native.cpp"], wrapped_functions=("i9100_gpio_output","i9100_gpio_release"))
if a.cross_compile:
    e.cross_compile(["i9100_modem_cp.c", "i9100_modem_cp_native.cpp"])
e.finish(["i9100_modem_cp.c", "i9100_modem_cp_native.cpp"],
         "XMM6260 CP GPIO control machine and native adapter models; polling only, "
         "no EHCI/PHY/IRQ/firmware or CP hardware evidence")
