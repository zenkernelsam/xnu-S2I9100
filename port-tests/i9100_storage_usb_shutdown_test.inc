/* SPDX-License-Identifier: BSD-2-Clause */
/* Actual command/ACK/poll gate executed against the existing USB fixture. */
static void storage_shutdown_tests(void)
{
 uctl_cancel();uctl_last=0;i9100_usb_busy1=0;uctl_reset_action=0;uctl_enabled=1;
 i9100_usb_panic_mode=0;i9100_usb_configured=1;i9100_pmu_map=1;
 i9100_usb_h=i9100_usb_t=0;UDTXFSTS(1)=384;pmu_count=barriers=halts=0;interrupts=1;
 i9100_storage_reboot_gate=1;i9100_storage_reboot_ready=i9100_storage_reboot_request=0;
 command("I9CTL1 00000001 reboot-download\n");CHECK(strstr(uctl_reply,"ERR storage-not-ready")&&!uctl_action&&!pmu_count);
 uctl_cancel();i9100_storage_reboot_gate=2;
 command("I9CTL1 00000002 reboot-download\n");CHECK(uctl_action==2&&!pmu_count&&!i9100_storage_reboot_request);
 i9100_usb_drain();CHECK(uctl_flight);uctl_complete(UEPINT_XFC);
 CHECK(uctl_reset_action==2&&!i9100_storage_reboot_request);
 fake_ticks+=UCTL_ACK_TICKS;uctl_reset_poll();CHECK(i9100_storage_reboot_request==2&&!pmu_count&&interrupts==1);
 i9100_storage_reboot_ready=1;uctl_reset_poll();CHECK(!pmu_count&&uctl_reset_action==2);
 i9100_storage_reboot_ready=3;uctl_reset_poll();CHECK(!pmu_count&&!uctl_reset_action&&interrupts==1);
 /* An acknowledged second session is still single shot; cannot overwrite
  * a prior failed request or reset while the device has unflushed data. */
 i9100_storage_reboot_ready=i9100_storage_reboot_request=0;i9100_usb_busy1=0;uctl_cancel();
 command("I9CTL1 00000003 reboot-download\n");i9100_usb_drain();uctl_complete(UEPINT_XFC);fake_ticks+=UCTL_ACK_TICKS;
 uctl_reset_poll();CHECK(!pmu_count&&i9100_storage_reboot_request==2);
 i9100_storage_reboot_ready=2;uctl_reset_poll();
 CHECK(pmu_count==3&&pmu_values[0]==0x12345678&&pmu_values[1]==0x12345671&&pmu_values[2]==1&&halts==1&&!interrupts);
 uctl_reset_poll();CHECK(pmu_count==3); /* One reset, no repeat. */
 /* Panic rescue is separate from ordinary journal-safe reboot. */
 pmu_count=barriers=halts=0;interrupts=1;i9100_usb_busy1=0;uctl_cancel();uctl_reset_action=0;
 i9100_usb_panic_mode=1;i9100_storage_reboot_gate=3;
 command("I9CTL1 00000004 reboot-download\n");
 /* Manual emergency rescue keeps the original unsynchronized PMU route;
  * it must not pretend to execute a journal-safe worker inside panic. */
 CHECK(uctl_action==2&&!pmu_count);i9100_usb_drain();uctl_complete(UEPINT_XFC);fake_ticks+=UCTL_ACK_TICKS;
 uctl_reset_poll();CHECK(pmu_count==3&&pmu_values[1]==0x12345671&&halts==1);
 uctl_cancel();i9100_usb_panic_mode=0;i9100_usb_busy1=0;interrupts=1;pmu_count=barriers=halts=0;
 i9100_storage_reboot_gate=i9100_storage_reboot_ready=i9100_storage_reboot_request=0;
 i9100_lpm_poweroff_ready=1;command("I9CTL1 00000005 poweroff-charge\n");CHECK(uctl_action==4);uctl_cancel();
 i9100_storage_reboot_gate=2;command("I9CTL1 00000006 poweroff-charge\n");CHECK(!uctl_action&&strstr(uctl_reply,"ERR disabled"));
 i9100_storage_reboot_gate=i9100_storage_reboot_ready=i9100_storage_reboot_request=0;
 printf("PASS actual USB command/ACK/poll storage gate: ordinary reset blocked until native seal, failed/no-retry, original PMU download sequence; explicit register/EPcompletion facade\n");
}
