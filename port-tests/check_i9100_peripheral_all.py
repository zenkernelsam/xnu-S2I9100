#!/usr/bin/env python3
"""Compile every offline peripheral object and optionally link a complete kernel.

Retains the active reference object's overrides and writes only to this out.
No generated BUILD rules, loader package, USB, serial, flashing or cleanup.
"""
import argparse
import hashlib
import json
import os
import shlex
import time
from pathlib import Path
from i9100_offline_build import Evidence, REPO, ROOT, PLATFORM, idle
from i9100_link_snapshot import freeze,verify
from i9100_power_shared_copy import prepare as prepare_shared_power

C_SOURCES = [
    "i9100_gpio_broker.c", "i9100_i2c_gpio.c", "i9100_clock_gates.c",
    "i9100_irq_registry.c", "i9100_dma_guard.c", "i9100_peripheral_board.c",
    "i9100_i2c_samsung.c", "i9100_irq_cascade.c", "i9100_audss.c",
    "i9100_sensors.c", "i9100_vibrator.c", "i9100_pl330.c", "i9100_i2s.c",
    "i9100_mc1n2.c", "i9100_pcm_stream.c", "i9100_sdio.c", "i9100_bcm4330.c", "i9100_bluetooth_h4.c",
    "i9100_camera_protocol.c", "i9100_mfc.c", "i9100_mfc_decode.c", "i9100_mfc_layout.c", "i9100_mfc_power.c", "i9100_mfc_host.c", "i9100_video_layout.c",
    "i9100_camera_engine_io.c", "i9100_csis.c", "i9100_fimc.c",
    "i9100_sysmmu.c", "i9100_modem_sipc4.c", "i9100_emmc.c", "i9100_emmc_layout.c", "i9100_emmc_writer.c",
    "i9100_uart_bluetooth.c", "i9100_pwm_haptic.c",
    "i9100_bcm_f2.c", "i9100_audio_power.c", "i9100_irq_router.c", "i9100_pcm_ring.c", "i9100_bcm_chip.c",
    "i9100_bcm_firmware.c", "i9100_bcm_firmware_crypto.c",
    "i9100_bcm_core_download.c",
    "i9100_radio_power.c",
    "i9100_bcm_boot.c",
    "i9100_wlan_control.c",
    "i9100_bcm_pump.c",
    "i9100_radio_survey.c",
    "i9100_audio_user_session.c",
    "i9100_pl330_ring.c",
    "i9100_sensor_hid_wire.c", "i9100_sensor_hid_queue.c",
    "i9100_bt_boot.c",
    "i9100_bt_probe.c", "i9100_bt_acl.c", "i9100_bt_stream.c",
    "i9100_emmc_host.c", "i9100_emmc_boot_contract.c", "i9100_emmc_clock.c", "i9100_emmc_idmac.c",
]
CPP_SOURCES = ["i9100_dma_native.cpp", "i9100_peripheral_native.cpp", "i9100_emmc_boot.cpp", "i9100_emmc_clock_native.cpp", "i9100_emmc_worker.cpp",
               "IOS7I9100EMMC.cpp", "IOS7I9100GPT.cpp", "i9100_audio_native.cpp", "i9100_audio_board_native.cpp", "i9100_audio_worker.cpp", "i9100_sensor_worker.cpp", "i9100_haptic_worker.cpp", "i9100_radio_native.cpp", "i9100_bcm_worker.cpp", "i9100_bcm_irq.cpp", "i9100_power_shared.cpp", "IOS6I9100AudioPCM.cpp", "i9100_pl330_ring_native.cpp", "i9100_pl330_ring_mmio.cpp", "i9100_audio_gated.cpp", "i9100_sensor_hid.cpp", "i9100_sensor_hid_endpoint.cpp", "i9100_bt_worker.cpp", "i9100_bt_control.cpp", "i9100_camera_native.cpp", "i9100_camera_mmio.cpp", "i9100_mfc_native.cpp", "i9100_mfc_board_native.cpp", "i9100_emmc_native.cpp", "IOS6I9100EMMCStorage.cpp", "IOS6I9100EMMCRaw.cpp", "IOS6I9100EMMCBlockDriver.cpp", "i9100_emmc_alias.cpp", "i9100_emmc_publication.cpp"]
TESTS = [
    ("i9100_emmc_clock_test.c", ["i9100_emmc_clock.c"]),
    ("i9100_emmc_clock_native_test.cpp", ["i9100_emmc_clock.c","i9100_emmc_host.c","i9100_emmc.c",
      "i9100_emmc_layout.c","i9100_emmc_writer.c","i9100_emmc_clock_native.cpp","i9100_camera_mmio.cpp","i9100_dma_guard.c","i9100_dma_native.cpp"]),
    ("i9100_emmc_boot_test.cpp", ["i9100_emmc.c","i9100_emmc_layout.c","i9100_emmc_writer.c","i9100_emmc_host.c","i9100_emmc_boot_contract.c","i9100_emmc_clock.c",
      "i9100_gpio_broker.c","i9100_clock_gates.c","i9100_irq_registry.c","i9100_peripheral_board.c","i9100_irq_cascade.c",
      "i9100_i2c_samsung.c","i9100_uart_bluetooth.c","i9100_sdio.c","i9100_bluetooth_h4.c","i9100_bcm_f2.c",
      "i9100_emmc_native.cpp","i9100_camera_mmio.cpp","IOS6I9100EMMCStorage.cpp","IOS6I9100EMMCRaw.cpp","IOS6I9100EMMCBlockDriver.cpp","i9100_emmc_alias.cpp",
      "i9100_emmc_publication.cpp","i9100_emmc_boot.cpp","i9100_peripheral_native.cpp"]),
    ("i9100_emmc_matching_test.cpp", ["i9100_emmc.c","i9100_emmc_layout.c","i9100_emmc_writer.c","i9100_emmc_host.c","i9100_emmc_boot_contract.c","i9100_emmc_clock.c",
      "i9100_gpio_broker.c","i9100_clock_gates.c","i9100_irq_registry.c","i9100_peripheral_board.c","i9100_irq_cascade.c",
      "i9100_i2c_samsung.c","i9100_uart_bluetooth.c","i9100_sdio.c","i9100_bluetooth_h4.c","i9100_bcm_f2.c",
      "i9100_emmc_native.cpp","i9100_camera_mmio.cpp","IOS6I9100EMMCStorage.cpp","IOS6I9100EMMCRaw.cpp","IOS6I9100EMMCBlockDriver.cpp","i9100_emmc_alias.cpp",
      "i9100_emmc_publication.cpp","i9100_emmc_boot.cpp","i9100_peripheral_native.cpp"]),
    ("i9100_emmc_native_test.cpp", ["i9100_emmc.c","i9100_emmc_layout.c","i9100_emmc_writer.c","i9100_emmc_host.c","i9100_emmc_clock.c",
        "i9100_emmc_native.cpp","i9100_camera_mmio.cpp"]),
    ("i9100_emmc_storage_test.cpp", ["i9100_emmc.c","i9100_emmc_layout.c","i9100_emmc_writer.c","i9100_emmc_host.c","i9100_emmc_clock.c",
        "i9100_emmc_native.cpp","i9100_camera_mmio.cpp","IOS6I9100EMMCStorage.cpp","IOS6I9100EMMCRaw.cpp","IOS6I9100EMMCBlockDriver.cpp","i9100_emmc_alias.cpp"]),
    ("i9100_emmc_writer_test.c", ["i9100_emmc.c","i9100_emmc_layout.c","i9100_emmc_writer.c"]),
    ("i9100_emmc_pio_test.c", ["i9100_emmc.c","i9100_emmc_layout.c","i9100_emmc_writer.c"]),
    ("i9100_emmc_sdr_test.c", ["i9100_emmc.c","i9100_emmc_layout.c","i9100_emmc_writer.c"]),
    ("i9100_emmc_sdr_native_test.cpp", ["i9100_emmc.c","i9100_emmc_layout.c","i9100_emmc_writer.c",
      "i9100_emmc_host.c","i9100_emmc_clock.c","i9100_emmc_native.cpp","i9100_camera_mmio.cpp"]),
    ("i9100_emmc_idmac_test.c",["i9100_emmc.c","i9100_emmc_idmac.c","i9100_dma_guard.c","i9100_emmc_layout.c","i9100_emmc_writer.c"]),
    ("i9100_emmc_idmac_native_test.cpp",["i9100_emmc.c","i9100_emmc_idmac.c","i9100_dma_guard.c","i9100_emmc_layout.c",
      "i9100_emmc_writer.c","i9100_emmc_host.c","i9100_emmc_clock.c","i9100_emmc_native.cpp","i9100_camera_mmio.cpp","i9100_dma_native.cpp"]),
    ("i9100_emmc_worker_test.cpp",["i9100_emmc.c","i9100_emmc_idmac.c","i9100_dma_guard.c","i9100_emmc_layout.c",
      "i9100_emmc_writer.c","i9100_emmc_host.c","i9100_emmc_clock.c","i9100_emmc_native.cpp","i9100_emmc_worker.cpp",
      "i9100_dma_native.cpp","i9100_camera_mmio.cpp","IOS6I9100EMMCStorage.cpp","IOS6I9100EMMCRaw.cpp","IOS6I9100EMMCBlockDriver.cpp","i9100_emmc_alias.cpp"]),
    ("i9100_mfc_native_test.cpp", ["i9100_mfc.c","i9100_mfc_decode.c","i9100_mfc_layout.c","i9100_dma_guard.c",
        "i9100_mfc_host.c","i9100_mfc_native.cpp","i9100_camera_mmio.cpp","i9100_dma_native.cpp"]),
    ("i9100_mfc_decode_test.c", ["i9100_mfc.c","i9100_mfc_decode.c","i9100_mfc_layout.c","i9100_dma_guard.c"]),
    ("i9100_mfc_power_test.c", ["i9100_mfc_power.c","i9100_mfc_host.c"]),
    ("i9100_mfc_board_native_test.cpp", ["i9100_mfc_power.c","i9100_mfc_host.c","i9100_mfc.c","i9100_mfc_decode.c","i9100_mfc_layout.c",
        "i9100_dma_guard.c","i9100_irq_registry.c","i9100_mfc_board_native.cpp","i9100_mfc_native.cpp","i9100_dma_native.cpp","i9100_camera_mmio.cpp"]),
    ("i9100_camera_capture_test.c", ["i9100_camera_engine_io.c","i9100_csis.c","i9100_fimc.c",
        "i9100_video_layout.c","i9100_dma_guard.c"]),
    ("i9100_camera_native_test.cpp", ["i9100_camera_engine_io.c","i9100_csis.c","i9100_fimc.c",
        "i9100_video_layout.c","i9100_dma_guard.c","i9100_camera_native.cpp","i9100_camera_mmio.cpp"]),
    ("i9100_bt_baud_test.c", ["i9100_bluetooth_h4.c","i9100_bt_boot.c"]),
    ("i9100_bt_host_reset_test.c", ["i9100_bluetooth_h4.c","i9100_bt_boot.c"]),
    ("i9100_bt_stream_test.c", ["i9100_bt_stream.c"]),
    ("i9100_bt_control_test.cpp", ["i9100_gpio_broker.c","i9100_clock_gates.c","i9100_irq_registry.c","i9100_peripheral_board.c",
        "i9100_i2c_samsung.c","i9100_uart_bluetooth.c","i9100_sdio.c","i9100_radio_power.c","i9100_irq_cascade.c",
        "i9100_bluetooth_h4.c","i9100_bt_boot.c","i9100_bt_probe.c","i9100_bt_acl.c","i9100_bt_stream.c","i9100_radio_native.cpp","i9100_bt_worker.cpp","i9100_bt_control.cpp"]),
    ("i9100_bt_probe_test.c", ["i9100_bluetooth_h4.c","i9100_bt_boot.c","i9100_bt_probe.c"]),
    ("i9100_bt_acl_test.c", ["i9100_bt_acl.c"]),
    ("i9100_bt_boot_test.c", ["i9100_bluetooth_h4.c","i9100_bt_boot.c"]),
    ("i9100_bt_worker_test.cpp", ["i9100_gpio_broker.c","i9100_clock_gates.c","i9100_irq_registry.c","i9100_peripheral_board.c",
        "i9100_i2c_samsung.c","i9100_uart_bluetooth.c","i9100_sdio.c","i9100_radio_power.c","i9100_irq_cascade.c",
        "i9100_bluetooth_h4.c","i9100_bt_boot.c","i9100_bt_probe.c","i9100_bt_acl.c","i9100_bt_stream.c","i9100_radio_native.cpp","i9100_bt_worker.cpp"]),
    ("i9100_sensor_hid_wire_test.c", ["i9100_sensor_hid_wire.c"]),
    ("i9100_sensor_hid_queue_test.c", ["i9100_sensor_hid_wire.c","i9100_sensor_hid_queue.c"]),
    ("i9100_sensor_hid_test.cpp", ["i9100_gpio_broker.c","i9100_clock_gates.c","i9100_irq_registry.c",
        "i9100_peripheral_board.c","i9100_i2c_samsung.c","i9100_uart_bluetooth.c","i9100_sdio.c",
        "i9100_irq_cascade.c","i9100_sensors.c","i9100_sensor_hid_wire.c","i9100_sensor_worker.cpp","i9100_sensor_hid.cpp"]),
    ("i9100_sensor_hid_endpoint_test.cpp", ["i9100_gpio_broker.c","i9100_clock_gates.c","i9100_irq_registry.c",
        "i9100_peripheral_board.c","i9100_i2c_samsung.c","i9100_uart_bluetooth.c","i9100_sdio.c",
        "i9100_irq_cascade.c","i9100_sensors.c","i9100_sensor_hid_wire.c","i9100_sensor_hid_queue.c",
        "i9100_sensor_worker.cpp","i9100_sensor_hid.cpp","i9100_sensor_hid_endpoint.cpp"]),
    ("i9100_sensor_hid_service_test.cpp", ["i9100_gpio_broker.c","i9100_clock_gates.c","i9100_irq_registry.c",
        "i9100_peripheral_board.c","i9100_i2c_samsung.c","i9100_uart_bluetooth.c","i9100_sdio.c",
        "i9100_irq_cascade.c","i9100_sensors.c","i9100_sensor_hid_wire.c","i9100_sensor_hid_queue.c",
        "i9100_sensor_worker.cpp","i9100_sensor_hid.cpp","i9100_sensor_hid_endpoint.cpp"]),
    ("i9100_audio_gated_user_test.cpp", ["i9100_audio_power.c","i9100_audss.c","i9100_mc1n2.c","i9100_dma_guard.c",
        "i9100_pl330.c","i9100_i2s.c","i9100_pcm_stream.c","i9100_gpio_broker.c","i9100_clock_gates.c",
        "i9100_peripheral_board.c","i9100_irq_registry.c","i9100_irq_router.c","i9100_pcm_ring.c","i9100_pl330_ring.c",
        "i9100_audio_user_session.c","i9100_audio_native.cpp","i9100_audio_board_native.cpp","i9100_audio_worker.cpp",
        "i9100_pl330_ring_native.cpp","i9100_pl330_ring_mmio.cpp","i9100_audio_gated.cpp","IOS6I9100AudioPCM.cpp"]),
    ("i9100_audio_gated_pdev_test.cpp", ["i9100_audio_power.c","i9100_audss.c","i9100_mc1n2.c","i9100_dma_guard.c",
        "i9100_pl330.c","i9100_i2s.c","i9100_pcm_stream.c","i9100_gpio_broker.c","i9100_clock_gates.c",
        "i9100_peripheral_board.c","i9100_irq_registry.c","i9100_irq_router.c","i9100_pcm_ring.c","i9100_pl330_ring.c",
        "i9100_audio_user_session.c","i9100_audio_native.cpp","i9100_audio_board_native.cpp","i9100_audio_worker.cpp",
        "i9100_pl330_ring_native.cpp","i9100_pl330_ring_mmio.cpp","i9100_audio_gated.cpp","IOS6I9100AudioPCM.cpp"]),
    ("i9100_audio_gated_pipeline_test.cpp", ["i9100_audio_power.c","i9100_audss.c","i9100_mc1n2.c","i9100_dma_guard.c",
        "i9100_pl330.c","i9100_i2s.c","i9100_pcm_stream.c","i9100_gpio_broker.c","i9100_clock_gates.c",
        "i9100_peripheral_board.c","i9100_irq_registry.c","i9100_irq_router.c","i9100_pcm_ring.c","i9100_pl330_ring.c",
        "i9100_audio_native.cpp","i9100_audio_board_native.cpp","i9100_audio_worker.cpp","i9100_pl330_ring_native.cpp",
        "i9100_pl330_ring_mmio.cpp","i9100_audio_gated.cpp"]),
    ("i9100_peripheral_bus_test.c", C_SOURCES[:5]),
    ("i9100_peripheral_platform_test.c", ["i9100_gpio_broker.c", "i9100_clock_gates.c",
        "i9100_peripheral_board.c", "i9100_i2c_samsung.c", "i9100_irq_cascade.c", "i9100_audss.c", "i9100_dma_guard.c"]),
    ("i9100_sensors_test.c", ["i9100_sensors.c", "i9100_vibrator.c"]),
    ("i9100_audio_transport_test.c", ["i9100_dma_guard.c", "i9100_pl330.c", "i9100_i2s.c", "i9100_mc1n2.c", "i9100_pcm_stream.c"]),
    ("i9100_audio_output_test.c", ["i9100_dma_guard.c", "i9100_pl330.c", "i9100_i2s.c", "i9100_mc1n2.c", "i9100_pcm_stream.c"]),
    ("i9100_wifi_bluetooth_test.c", ["i9100_sdio.c", "i9100_bcm4330.c", "i9100_bluetooth_h4.c"]),
    ("i9100_camera_mfc_test.c", ["i9100_camera_protocol.c", "i9100_mfc.c", "i9100_mfc_decode.c", "i9100_mfc_layout.c", "i9100_video_layout.c", "i9100_sysmmu.c", "i9100_dma_guard.c"]),
    ("i9100_modem_test.c", ["i9100_modem_sipc4.c"]),
    ("i9100_uart_pwm_test.c", ["i9100_uart_bluetooth.c", "i9100_pwm_haptic.c"]),
    ("i9100_bcm_f2_test.c", ["i9100_bcm_f2.c"]),
    ("i9100_audio_native_test.cpp", ["i9100_dma_guard.c", "i9100_pl330.c", "i9100_i2s.c",
        "i9100_pcm_stream.c", "i9100_mc1n2.c", "i9100_gpio_broker.c", "i9100_clock_gates.c",
        "i9100_peripheral_board.c", "i9100_audio_native.cpp"]),
    ("i9100_audio_power_test.c", ["i9100_audio_power.c"]),
    ("i9100_audio_board_test.cpp", ["i9100_audio_power.c", "i9100_audss.c", "i9100_mc1n2.c",
        "i9100_dma_guard.c", "i9100_pl330.c", "i9100_i2s.c", "i9100_pcm_stream.c",
        "i9100_gpio_broker.c", "i9100_clock_gates.c", "i9100_peripheral_board.c",
        "i9100_audio_native.cpp", "i9100_audio_board_native.cpp"]),
    ("i9100_audio_worker_test.cpp", ["i9100_audio_power.c", "i9100_audss.c", "i9100_mc1n2.c",
        "i9100_dma_guard.c", "i9100_pl330.c", "i9100_i2s.c", "i9100_pcm_stream.c",
        "i9100_gpio_broker.c", "i9100_clock_gates.c", "i9100_peripheral_board.c", "i9100_irq_registry.c", "i9100_irq_router.c", "i9100_pcm_ring.c",
        "i9100_audio_native.cpp", "i9100_audio_board_native.cpp", "i9100_audio_worker.cpp"]),
    ("i9100_pcm_ring_test.c", ["i9100_pcm_ring.c"]),
    ("i9100_bcm_chip_test.c", ["i9100_bcm4330.c", "i9100_bcm_chip.c"]),
    ("i9100_bcm_firmware_test.c", ["i9100_bcm4330.c", "i9100_bcm_chip.c", "i9100_bcm_firmware.c"]),
    ("i9100_bcm_core_download_test.c", ["i9100_bcm4330.c", "i9100_bcm_chip.c", "i9100_bcm_firmware.c", "i9100_bcm_core_download.c"]),
    ("i9100_native_transport_test.cpp", ["i9100_gpio_broker.c", "i9100_clock_gates.c", "i9100_irq_registry.c", "i9100_peripheral_board.c",
        "i9100_i2c_samsung.c", "i9100_uart_bluetooth.c", "i9100_sdio.c", "i9100_irq_cascade.c"]),
    ("i9100_native_sdio_success_test.cpp", ["i9100_gpio_broker.c", "i9100_clock_gates.c", "i9100_irq_registry.c", "i9100_peripheral_board.c",
        "i9100_i2c_samsung.c", "i9100_uart_bluetooth.c", "i9100_sdio.c", "i9100_irq_cascade.c"]),
    ("i9100_native_i2c_test.cpp", ["i9100_gpio_broker.c", "i9100_clock_gates.c", "i9100_irq_registry.c", "i9100_peripheral_board.c",
        "i9100_i2c_samsung.c", "i9100_uart_bluetooth.c", "i9100_sdio.c", "i9100_irq_cascade.c"]),
    ("i9100_sensor_worker_test.cpp", ["i9100_gpio_broker.c", "i9100_clock_gates.c", "i9100_irq_registry.c", "i9100_peripheral_board.c",
        "i9100_i2c_samsung.c", "i9100_uart_bluetooth.c", "i9100_sdio.c", "i9100_irq_cascade.c", "i9100_sensors.c", "i9100_sensor_worker.cpp"]),
    ("i9100_radio_power_test.c", ["i9100_gpio_broker.c", "i9100_peripheral_board.c", "i9100_radio_power.c"]),
    ("i9100_radio_native_test.cpp", ["i9100_gpio_broker.c", "i9100_clock_gates.c", "i9100_irq_registry.c", "i9100_peripheral_board.c",
        "i9100_i2c_samsung.c", "i9100_uart_bluetooth.c", "i9100_sdio.c", "i9100_radio_power.c", "i9100_radio_native.cpp", "i9100_irq_cascade.c"]),
    ("i9100_bcm_boot_test.c", ["i9100_bcm4330.c", "i9100_bcm_chip.c", "i9100_bcm_core_download.c", "i9100_bcm_firmware.c", "i9100_bcm_f2.c", "i9100_bcm_boot.c"]),
    ("i9100_wlan_control_test.c", ["i9100_bcm4330.c", "i9100_bcm_f2.c", "i9100_wlan_control.c"]),
    ("i9100_bcm_pump_test.c", ["i9100_bcm4330.c", "i9100_bcm_chip.c", "i9100_bcm_core_download.c", "i9100_bcm_firmware.c", "i9100_bcm_f2.c", "i9100_bcm_boot.c", "i9100_wlan_control.c", "i9100_bcm_pump.c"]),
    ("i9100_bcm_worker_test.cpp", ["i9100_bcm4330.c", "i9100_bcm_chip.c", "i9100_bcm_core_download.c", "i9100_bcm_firmware.c", "i9100_bcm_f2.c", "i9100_bcm_boot.c", "i9100_wlan_control.c", "i9100_bcm_pump.c", "i9100_bcm_worker.cpp"]),
    ("i9100_bcm_irq_test.cpp", ["i9100_gpio_broker.c", "i9100_clock_gates.c", "i9100_irq_registry.c", "i9100_irq_cascade.c", "i9100_irq_router.c", "i9100_peripheral_board.c", "i9100_i2c_samsung.c", "i9100_uart_bluetooth.c", "i9100_sdio.c", "i9100_bcm_irq.cpp"]),
    ("i9100_power_shared_test.cpp", ["i9100_power_bus.c", "i9100_power.c", "i9100_radio_survey.c", "i9100_power_shared.cpp"]),
    ("i9100_power_haptic_test.cpp", ["i9100_power_bus.c", "i9100_power.c", "i9100_power_shared.cpp"]),
    ("i9100_haptic_worker_test.cpp", ["i9100_gpio_broker.c", "i9100_clock_gates.c", "i9100_irq_registry.c", "i9100_irq_cascade.c", "i9100_peripheral_board.c",
        "i9100_i2c_samsung.c", "i9100_uart_bluetooth.c", "i9100_sdio.c", "i9100_pwm_haptic.c", "i9100_vibrator.c", "i9100_power_bus.c", "i9100_power.c", "i9100_power_shared.cpp", "i9100_haptic_worker.cpp"]),
    ("i9100_radio_survey_test.c", ["i9100_radio_survey.c"]),
    ("i9100_audio_user_test.c", ["i9100_audio_user_session.c"]),
    ("i9100_pl330_ring_test.c", ["i9100_dma_guard.c","i9100_i2s.c","i9100_pl330_ring.c"]),
    ("i9100_pl330_ring_native_test.cpp", ["i9100_dma_guard.c","i9100_i2s.c","i9100_clock_gates.c",
        "i9100_peripheral_board.c","i9100_pl330_ring.c","i9100_pl330_ring_native.cpp","i9100_pl330_ring_mmio.cpp"]),
    ("i9100_audio_user_native_test.cpp", ["i9100_audio_power.c", "i9100_audss.c", "i9100_mc1n2.c",
        "i9100_dma_guard.c", "i9100_pl330.c", "i9100_i2s.c", "i9100_pcm_stream.c", "i9100_gpio_broker.c",
        "i9100_clock_gates.c", "i9100_peripheral_board.c", "i9100_irq_registry.c", "i9100_irq_router.c",
        "i9100_pcm_ring.c", "i9100_audio_user_session.c", "i9100_audio_native.cpp", "i9100_audio_board_native.cpp",
        "i9100_audio_worker.cpp", "IOS6I9100AudioPCM.cpp"]),
    ("i9100_audio_pdev_test.cpp", ["i9100_audio_power.c", "i9100_audss.c", "i9100_mc1n2.c",
        "i9100_dma_guard.c", "i9100_pl330.c", "i9100_i2s.c", "i9100_pcm_stream.c", "i9100_gpio_broker.c",
        "i9100_clock_gates.c", "i9100_peripheral_board.c", "i9100_irq_registry.c", "i9100_irq_router.c",
        "i9100_pcm_ring.c", "i9100_audio_user_session.c", "i9100_audio_native.cpp", "i9100_audio_board_native.cpp",
        "i9100_audio_worker.cpp", "IOS6I9100AudioPCM.cpp"]),
]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--link", action="store_true")
    p.add_argument("--models", action="store_true")
    p.add_argument("--models-only", action="store_true", help="Run registered models without ARM compilation or linking")
    p.add_argument("--model-suite", action="append", default=[], metavar="TEST", help="Run only a named registered model; implies --models")
    p.add_argument("--power-shared", action="store_true", help="Link paired copied Power/bus/PE integration; no live source changes")
    p.add_argument("--reference", type=Path, help="successful retained control-* kernel directory")
    p.add_argument("--wait-idle", type=int, default=0, choices=range(0, 61), metavar="0..60")
    args = p.parse_args()
    if args.models_only and (args.link or args.power_shared):
        p.error("--models-only cannot also request --link or --power-shared")
    registered = {name for name, _ in TESTS}
    if len(registered) != len(TESTS):
        raise RuntimeError("Registered model suites are duplicated")
    if len(set(args.model_suite)) != len(args.model_suite) or set(args.model_suite) - registered:
        p.error("Use unique registered --model-suite names")
    selected = [(name, sources) for name, sources in TESTS if not args.model_suite or name in args.model_suite]
    models_requested = args.models or args.models_only or bool(args.model_suite)
    tested_sources = set()
    e = Evidence("peripheral-all-offline")
    e.wait_idle_seconds = args.wait_idle
    deadline = time.monotonic() + args.wait_idle
    while True:
        try:
            idle()
            break
        except RuntimeError:
            if time.monotonic() >= deadline:
                raise
            time.sleep(2)
    if models_requested:
        shared_copies = prepare_shared_power(e) if args.power_shared or any(name in (
            'i9100_power_shared_test.cpp','i9100_power_haptic_test.cpp','i9100_haptic_worker_test.cpp') for name,_ in selected) else {}
        for test, sources in selected:
            if any(name in sources for name in ("i9100_audio_board_native.cpp","i9100_audio_worker.cpp")):
                sources=list(sources)+[name for name in ("i9100_pl330_ring.c","i9100_audio_gated.cpp",
                    "i9100_pl330_ring_native.cpp","i9100_pl330_ring_mmio.cpp") if name not in sources]
            emmc_native=('i9100_emmc_native_test.cpp','i9100_emmc_storage_test.cpp','i9100_emmc_boot_test.cpp',
              'i9100_emmc_matching_test.cpp','i9100_emmc_sdr_native_test.cpp','i9100_emmc_idmac_native_test.cpp','i9100_emmc_worker_test.cpp')
            if test in emmc_native:
                sources=list(sources)+[name for name in ('i9100_emmc_idmac.c','i9100_dma_guard.c','i9100_dma_native.cpp')if name not in sources]
            if test in ('i9100_emmc_storage_test.cpp','i9100_emmc_boot_test.cpp','i9100_emmc_matching_test.cpp'):
                sources=list(sources)+[name for name in ('i9100_emmc_worker.cpp',)if name not in sources]
            tested_sources.update(sources)
            folder = e.out / Path(test).stem
            folder.mkdir()
            previous = e.out
            e.out = folder
            if test.endswith(".cpp"):
                test_override=None;host_defines=()
                overrides=shared_copies if test in ("i9100_power_shared_test.cpp","i9100_power_haptic_test.cpp","i9100_haptic_worker_test.cpp") else None
                extra=[]
                if test in ("i9100_audio_pdev_test.cpp","i9100_audio_gated_pdev_test.cpp"):
                    captured={}
                    for name in ("i9100-pdev.c","i9100-pdev.h"):
                        raw=(REPO/"userland/audio"/name).read_bytes();(folder/name).write_bytes(raw)
                        captured[name]=hashlib.sha256(raw).hexdigest()
                    e.result["pdev_sources"]=captured;extra=["i9100-pdev.c"]
                    overrides={"i9100-pdev.c":folder/"i9100-pdev.c"}
                if test in emmc_native or test=='i9100_emmc_clock_native_test.cpp':
                    historical=ROOT/"assets/i9100-backup/mmcblk0-head1mb.img"
                    if hashlib.sha256(historical.read_bytes()).hexdigest()!="879334a928bbffffc67d57b584906c03bee10f1310a88a0f1362fc6dfb3724ef":raise RuntimeError("Historical emmc fixture changed")
                    os.environ["I9100_EMMC_HISTORICAL_SOURCE"]=str(historical)
                    if test=='i9100_emmc_boot_test.cpp':
                        os.environ['I9100_EMMC_BOOT_MODEL_RECORD']=str(folder/'actual-contract.bin')
                        os.environ['I9100_EMMC_BOOT_MODEL_EVIDENCE']=str(folder)
                    for name in ("i9100_emmc_native_fixture.c","i9100_emmc_writer_test.c","i9100_emmc_test.c"):
                        (folder/name).write_bytes((REPO/"port-tests"/name).read_bytes())
                    extra=["i9100_emmc_native_fixture.c"];overrides={extra[0]:folder/extra[0]}
                    if test in emmc_native or test=='i9100_emmc_clock_native_test.cpp':
                        from i9100_emmc_dma_model import prepare as prepare_dma
                        overrides.update(prepare_dma(e))
                    if test=='i9100_emmc_matching_test.cpp':
                        from check_i9100_emmc_matching import prepare as prepare_matching
                        test_override,_,_=prepare_matching(e);host_defines=('I9100_EMMC_MATCH_ENGINE=1',)
                        (test_override.parent/'i9100_emmc_dma_facade.inc').write_bytes((REPO/'port-tests/i9100_emmc_dma_facade.inc').read_bytes())
                if test in ('i9100_mfc_native_test.cpp','i9100_mfc_board_native_test.cpp'):
                    from i9100_mfc_dma_model import prepare as prepare_mfc_dma
                    overrides=prepare_mfc_dma(e);host_defines=('I9100_MFC_ACTUAL_ALLOCATOR=1',) if test=='i9100_mfc_native_test.cpp'else ()
                e.model_cpp(test, [name for name in sources if name.endswith(".c")]+extra,
                            [name for name in sources if name.endswith(".cpp")],
                            libraries=["crypto"] if test in ("i9100_bcm_worker_test.cpp","i9100_bt_worker_test.cpp","i9100_bt_control_test.cpp","i9100_mfc_native_test.cpp","i9100_mfc_board_native_test.cpp") else ["pthread"] if test in emmc_native or test in ("i9100_power_shared_test.cpp","i9100_power_haptic_test.cpp","i9100_haptic_worker_test.cpp","i9100_emmc_clock_native_test.cpp") else (),
                            source_overrides=overrides,
                            wrapped_functions=("i9100_sdio_init",) if test=="i9100_native_sdio_success_test.cpp" else
                            ("i9100_uart_bt_init","i9100_bt_worker_stream_open") if test=="i9100_bt_control_test.cpp" else
                            ("i9100_uart_bt_init",) if test=="i9100_bt_worker_test.cpp" else
                            ("i9100_i2c_samsung_init",) if test in ("i9100_native_i2c_test.cpp","i9100_sensor_worker_test.cpp",
                                "i9100_sensor_hid_test.cpp","i9100_sensor_hid_endpoint_test.cpp","i9100_sensor_hid_service_test.cpp") else
                            ("i9100_ring_mmio_read","i9100_ring_mmio_write","pthread_mutex_init","pthread_mutex_lock","pthread_mutex_unlock") if test=="i9100_audio_gated_pdev_test.cpp" else
                            ("pthread_mutex_init","pthread_mutex_lock","pthread_mutex_unlock") if test=="i9100_audio_pdev_test.cpp" else
                            ("i9100_camera_mmio_read","i9100_camera_mmio_write") if test in emmc_native or test in ("i9100_camera_native_test.cpp","i9100_mfc_native_test.cpp","i9100_mfc_board_native_test.cpp","i9100_emmc_clock_native_test.cpp") else
                            ("i9100_ring_mmio_read","i9100_ring_mmio_write") if test in
                            ("i9100_pl330_ring_native_test.cpp","i9100_audio_gated_pipeline_test.cpp","i9100_audio_gated_user_test.cpp") else (),
                            extra_sdk=REPO/'port-tests/bt-control-sdk' if test=='i9100_bt_control_test.cpp' else
                            REPO/'port-tests/emmc-storage-sdk' if test in ('i9100_emmc_storage_test.cpp','i9100_emmc_boot_test.cpp','i9100_emmc_matching_test.cpp','i9100_emmc_clock_native_test.cpp','i9100_emmc_worker_test.cpp') else
                            REPO/'port-tests/emmc-native-sdk' if test in ('i9100_emmc_native_test.cpp','i9100_emmc_sdr_native_test.cpp','i9100_emmc_idmac_native_test.cpp') else
                            REPO/'port-tests/dma-native-sdk' if test in ('i9100_mfc_native_test.cpp','i9100_mfc_board_native_test.cpp') else None,
                            test_override=test_override,extra_defines=host_defines)
                if test=="i9100_power_shared_test.cpp":
                    for n in range(1,4):e.run([str(e.out/"models"),str(n)],"shared-prepare-failure-"+str(n)+"-tests")
                if test=="i9100_emmc_clock_native_test.cpp":
                    for scenario in ('conflict-data','conflict-legacy','conflict-root','conflict-rootdev','bad-root-clock','busy-hook'):
                        e.run([str(e.out/'models'),scenario],'hook-'+scenario+'-tests')
                if test=='i9100_emmc_sdr_native_test.cpp':
                    for fault in ('switch-error','restore-error','extcsd-corrupt','pad-readback','pad-restore'):
                        e.run([str(e.out/'models'),fault],'fault-'+fault+'-tests')
                if test=='i9100_emmc_idmac_native_test.cpp':
                    for fault in ('timeout','short-counter','owned-descriptor','stop-drop'):
                        e.run([str(e.out/'models'),fault],'fault-'+fault+'-tests')
                if test=='i9100_emmc_worker_test.cpp':
                    for fault in ('hardware-failure','stop-timeout','memory-complete-error','remove-error'):
                        e.run([str(e.out/'models'),fault],'fault-'+fault+'-tests')
                if test in ("i9100_audio_pdev_test.cpp","i9100_audio_gated_pdev_test.cpp") and any(
                    hashlib.sha256((REPO/"userland/audio"/name).read_bytes()).hexdigest()!=sha for name,sha in captured.items()):
                    raise RuntimeError("Pdev input changed during the model")
            else:
                if test=="i9100_emmc_clock_test.c":
                    e.run(['clang','-std=c99','-O1','-Wall','-Wextra','-Werror','-fno-pie','-no-pie','-fsanitize=address,undefined','-fno-sanitize-recover=all',
                        '-I'+str(PLATFORM),str(REPO/'port-tests'/test),str(PLATFORM/'i9100_emmc_clock.c'),'-o',str(folder/'models')],'compile-clock-model')
                    e.run([str(folder/'models'),str(folder)],'clock-model-tests')
                elif test in ("i9100_emmc_writer_test.c","i9100_emmc_pio_test.c","i9100_emmc_sdr_test.c","i9100_emmc_idmac_test.c"):
                    historical=ROOT/"assets/i9100-backup/mmcblk0-head1mb.img"
                    digest=hashlib.sha256(historical.read_bytes()).hexdigest()
                    if digest!="879334a928bbffffc67d57b584906c03bee10f1310a88a0f1362fc6dfb3724ef":
                        raise RuntimeError("Historical primary GPT/PIT changed")
                    e.run(["clang","-std=c99","-O1","-Wall","-Wextra","-Werror","-fno-pie","-no-pie",
                        "-fsanitize=address,undefined","-fno-sanitize-recover=all","-I"+str(PLATFORM),
                        str(REPO/"port-tests"/test),*[str(PLATFORM/name) for name in sources],"-o",str(folder/"models")],"compile-writer-models")
                    e.run([str(folder/"models"),str(historical)],"writer-model-tests")
                    e.result["writer_historical_primary_sha256"]=digest
                else:
                    e.model(test, sources, libraries=["crypto"] if test in ("i9100_bcm_firmware_test.c","i9100_bcm_core_download_test.c","i9100_bcm_boot_test.c","i9100_bcm_pump_test.c","i9100_bt_boot_test.c","i9100_bt_probe_test.c","i9100_bt_host_reset_test.c","i9100_bt_baud_test.c","i9100_camera_mfc_test.c","i9100_mfc_decode_test.c") else ())
            e.out = previous
        e.result["model_suites"] = [name for name, _ in selected]
        e.result["audio_model_fixtures_sha256"] = {
            name: hashlib.sha256((REPO / "port-tests" / name).read_bytes()).hexdigest()
            for name in ("i9100_audio_transport_test.c", "i9100_audio_native_test.cpp", "i9100_audio_board_test.cpp")
        }
    if args.models_only:
        e.result["models_only"] = True
        e.finish(sorted(tested_sources), "Selected registered peripheral models only; no ARM compilation, kernel link, loader package or physical hardware")
        return
    e.cross_compile(C_SOURCES + CPP_SOURCES)
    if args.power_shared:
        if not models_requested:shared_copies = prepare_shared_power(e)
        e.cross_compile(["i9100_power_bus.c","IOS7I9100Power.cpp","AppleARMPE.cpp"],shared_copies)
        e.result["peripheral_objects"] = len(C_SOURCES + CPP_SOURCES)
    if args.link:
        if args.reference:
            reference = args.reference.resolve()
        else:
            candidates = [folder for folder in sorted((REPO / "out/automation").glob("control-*"), reverse=True)
                          if (folder / "mach_kernel").exists() and (folder / "link.filelist").exists()]
            if not candidates:
                raise RuntimeError("No complete reference kernel")
            reference = candidates[0]
        if not reference.is_relative_to(REPO / "out/automation"):
            raise RuntimeError("Reference must remain under the retained repo out/automation")
        reference_hash = hashlib.sha256((reference / "mach_kernel").read_bytes()).hexdigest()
        base_list = (reference / "link.filelist").read_text().splitlines()
        if not base_list or any(not Path(name).is_file() for name in base_list):
            raise RuntimeError("Incomplete reference objects; do not silently replace their overrides")
        extra = [str(ROOT / item["object"]) for item in e.commands]
        known = set(base_list)
        if known.intersection(extra):
            raise RuntimeError("New objects duplicate reference entries")
        build = REPO / "BUILD/obj/RELEASE_ARM_EXYNOS4210_I9100"
        # A retained filelist often still names shared BUILD/*.o. The older
        # reference mach_kernel hash alone cannot freeze those inputs. Copy
        # every input before linking and retain its exact bytes/hash.
        idle()
        stable_base,base_snapshot=freeze(base_list,e.out/"frozen-base")
        excluded=[]
        if args.power_shared:
            for name in ("i9100_power_bus.o","IOS7I9100Power.cpo","AppleARMPE.cpo"):
                found=[i for i,path in enumerate(base_list) if Path(path).name==name]
                if len(found)!=1:raise RuntimeError("Reference override is ambiguous: "+name)
                excluded.extend(found)
            e.result["replaced_reference_objects"] = [base_list[i] for i in excluded]
        linked_base=[path for i,path in enumerate(stable_base) if i not in excluded]
        stable_aux,aux_snapshot=freeze([build/"version.o",build/"lastkernelconstructor.o"],e.out/"frozen-aux")
        filelist = e.out / "link.filelist"
        filelist.write_text("\n".join(linked_base + extra) + "\n")
        command = shlex.split((reference / "link-flags.txt").read_text().strip().splitlines()[-1])
        command.extend(["-filelist", str(filelist), *stable_aux, "-o", str(e.out / "mach_kernel.sys")])
        env = dict(os.environ, TC=str(ROOT / "assets/i9100-toolchain"), SDKROOT="/",
                   LD64_WRAPPER=str(ROOT / "assets/i9100-toolchain/bin/ld64"),
                   SRCROOT=str(REPO), OBJROOT=str(REPO / "BUILD/obj"),
                   SYMROOT=str(REPO / "BUILD/sym"), TARGET=str(build))
        env["PATH"] = str(ROOT / "assets/i9100-toolchain/bin") + ":" + env["PATH"]
        e.run(command, "complete-kernel-link", env, cwd=build)
        verify(base_snapshot)
        verify(aux_snapshot)
        if hashlib.sha256((reference / "mach_kernel").read_bytes()).hexdigest() != reference_hash:
            raise RuntimeError("Reference changed during linking; result cannot be called a fixed comparison")
        e.result.update(kernel_reference=str(reference.relative_to(ROOT)),
                        reference_sha256=reference_hash, reference_objects=len(base_list),
                        new_objects=len(extra), linked_kernel_sha256=hashlib.sha256((e.out / "mach_kernel.sys").read_bytes()).hexdigest(),
                        frozen_input_manifests=[str(path.relative_to(ROOT)) for path in (base_snapshot,aux_snapshot)],
                        reference_mode="Historical reference filelist; actual object bytes frozen at this run")
    e.finish(C_SOURCES + CPP_SOURCES, "Offline default-OFF peripheral objects; no loader package or live hardware")
    if args.link:
        result = json.loads((e.out / "manifest.json").read_text())
        result["complete_kernel_link"] = True
        (e.out / "manifest.json").write_text(json.dumps(result, indent=2) + "\n")


if __name__ == "__main__":
    main()
