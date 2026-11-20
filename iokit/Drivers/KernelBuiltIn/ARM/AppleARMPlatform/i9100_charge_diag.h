#ifndef I9100_CHARGE_DIAG_H
#define I9100_CHARGE_DIAG_H
#include <stdint.h>
#include "i9100_power.h"
#ifdef __cplusplus
extern "C" {
#endif

/* The existing Power workloop is the only caller/writer. Fixed, non-IRQ
 * registers only; no new bus instance, charger writes or current-mode pulses.
 * Current/AvgCurrent remain raw: their calibration must be proved on U1.
 * v2 adds raw ADC1/probe; v3 adds a failed-disable/readback indicator in word39.
 * No calibrated mA claim.
 * {version, sequence, stride, capacity}, then 16 published 40-word samples. */
volatile uint32_t i9100_charge_diag[4 + 16 * 40] = {3, 0, 40, 16};
static unsigned i9100_charge_phase, i9100_charge_probe_result;

static void i9100_charge_snapshot(const struct i9100_power_state *s,
    struct i9100_power_hw *hw, unsigned polls, uint64_t now)
{
    static const unsigned fuel_regs[5] = {0x0a, 0x0b, 0x1d, 0x2b, 0x2e};
    static const unsigned pmic_regs[8] = {0x10, 0x50, 0x51, 0x52,
        0x53, 0x54, 0x55, 0x5a};
    static const unsigned muic_regs[2] = {0x0d, 0x05};
    if (!s || !hw || !s->ready || !s->io.fuel_read ||
        !s->io.pmic_read || !hw->read32) return;
    unsigned sequence = i9100_charge_diag[1] + 1U;
    if (!sequence) sequence = 1;
    volatile uint32_t *r = &i9100_charge_diag[4 + ((sequence - 1U) & 15U) * 40];
    r[0] = 0;
    for (unsigned n = 1; n < 40; n++) r[n] = 0;
    r[1] = polls; r[2] = (uint32_t)now; r[3] = (uint32_t)(now >> 32);
    r[4] = s->revision; r[5] = s->use_sub;
    r[6] = s->valid ? 0x80000000U : 0;
    r[7] = (uint32_t)s->reason;
    r[8] = s->percent; r[9] = s->millivolts;
    r[10] = (uint32_t)s->temperature_deci_c;
    r[11] = s->enabled; r[12] = s->connected; r[13] = s->charging;
    r[14] = s->full; r[15] = s->timeout; r[16] = s->recharging;
    r[17] = (uint32_t)(i9100_power_elapsed(s->charge_start,s->charge_elapsed_base,now)/1000000000ULL);
    for (unsigned n = 0; n < 5; n++) {
        uint16_t value = 0;
        r[18 + n] = 0xffffffffU;
        if (s->io.fuel_read(s->io.cookie, fuel_regs[n], &value)) {
            r[18 + n] = value; r[6] |= 1U << n;
        }
    }
    for (unsigned n = 0; n < 8; n++) {
        uint8_t value = 0;
        r[23 + n] = 0xffffffffU;
        if (s->io.pmic_read(s->io.cookie, pmic_regs[n], &value)) {
            r[23 + n] = value; r[6] |= 1U << (5 + n);
        }
    }
    for (unsigned n = 0; n < 2; n++) {
        uint8_t value = 0;
        r[31 + n] = 0xffffffffU;
        if (s->io.muic_read && s->io.muic_read(s->io.cookie, muic_regs[n], &value)) {
            r[31 + n] = value; r[6] |= 1U << (13 + n);
        }
    }
    /* Retain GPL2 configuration and levels without changing their mux/pull. */
    for (unsigned n = 0; n < 3; n++)
        r[33 + n] = hw->read32(hw->cookie, 1, 0x100 + n * 4);
    unsigned adc=0;
    r[36]=0xffffffffU;
    if(i9100_power_charge_adc_read(hw,&adc)){r[36]=adc;r[6]|=1U<<15;}
    r[37]=i9100_charge_phase;r[38]=i9100_charge_probe_result;
    r[39]=s->stop_uncertain;
    __sync_synchronize(); r[0] = sequence;
    __sync_synchronize(); i9100_charge_diag[1] = sequence;
}
/* Explicit development probe, once per boot, on the original Power owner.
 * One second disabled, then the ORIGINAL policy decides whether to restore.
 * Do not reset safety deadlines or generate TA programming pulses. */
static void i9100_charge_probe(struct i9100_power_state *s,
    struct i9100_power_hw *hw,unsigned polls,void (*sleep_ms)(unsigned))
{
    i9100_charge_probe_result=0;
    if(!s || !hw || !sleep_ms || !s->ready || !s->io.now_ns || !s->valid || !s->present || !s->connected ||
       !s->enabled || !s->charging || !s->use_sub || s->revision!=14 ||
       s->percent<20 || s->millivolts<3700 || s->temperature_deci_c<50 || s->temperature_deci_c>400 ||
       s->full || s->timeout || s->thermal_block){
        i9100_charge_phase=4;i9100_charge_probe_result=1;
        if(s && hw && s->io.now_ns)i9100_charge_snapshot(s,hw,polls,s->io.now_ns(s->io.cookie));
        i9100_charge_phase=0;return;
    }
    i9100_charge_phase=1;
    i9100_charge_snapshot(s,hw,polls,s->io.now_ns(s->io.cookie));
    if(!i9100_power_stop(s)){
        i9100_charge_phase=4;i9100_charge_probe_result=2;
        i9100_charge_snapshot(s,hw,polls,s->io.now_ns(s->io.cookie));
        i9100_charge_phase=0;return;
    }
    sleep_ms(1000);
    i9100_charge_phase=2;
    i9100_charge_snapshot(s,hw,polls,s->io.now_ns(s->io.cookie));
    int result=i9100_power_poll(s);
    i9100_charge_phase=3;
    if(result || !s->enabled || !s->charging)i9100_charge_probe_result=3;
    i9100_charge_snapshot(s,hw,polls,s->io.now_ns(s->io.cookie));
    i9100_charge_phase=0;
}
#ifdef __cplusplus
}
#endif
#endif
