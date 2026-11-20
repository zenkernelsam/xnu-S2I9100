#ifndef I9100_LOW_BATTERY_H
#define I9100_LOW_BATTERY_H
#include <stdint.h>
#include "i9100_power.h"

/* Reserve qualification for this board port, not gauge recalibration or a
 * charger limit. The standalone loader admits boot at >=3% and >=3600mV;
 * reserve retreat uses <=3% AND <=3400mV with a real four-second window.
 * A pending normal halt is one-shot; only the existing BSD worker executes
 * it. No PMU writes, forced shutdown, retries or charging changes here. */
#define I9100_LOW_BATTERY_MV 3400U
#define I9100_LOW_BATTERY_SOC 3U
#define I9100_LOW_BATTERY_SAMPLES 5U
#define I9100_LOW_BATTERY_SECONDS 4U
struct i9100_low_battery_state {
 unsigned enabled,samples,admitted,submitted,trigger_percent,trigger_mv;
 uint64_t first_ns,last_ns,submitted_ns;
};
static inline void i9100_low_battery_update(struct i9100_low_battery_state *state,
 const struct i9100_power_state *battery,uint64_t now)
{
 if(!state || state->submitted)return;
 unsigned low=state->enabled && battery && battery->ready && battery->valid &&
  battery->present && !battery->stop_uncertain && battery->percent<=I9100_LOW_BATTERY_SOC &&
  battery->millivolts>=2500U && battery->millivolts<=I9100_LOW_BATTERY_MV;
 if(!low || !now || (state->last_ns &&
    (now<=state->last_ns || now-state->last_ns>2500000000ULL))){
  state->samples=state->admitted=0;state->first_ns=0;state->last_ns=now;return;
 }
 if(!state->samples)state->first_ns=now;
 if(state->samples<I9100_LOW_BATTERY_SAMPLES)state->samples++;
 state->last_ns=now;
 state->admitted=state->samples>=I9100_LOW_BATTERY_SAMPLES && now>=state->first_ns &&
  now-state->first_ns>=(uint64_t)I9100_LOW_BATTERY_SECONDS*1000000000ULL;
}
static inline void i9100_low_battery_publish(volatile uint32_t words[18],
 const struct i9100_low_battery_state *state,const struct i9100_power_state *battery,unsigned request)
{
 unsigned sequence=(words[1]&~1U)+2U;
 words[1]=sequence-1U;__sync_synchronize();
 words[0]=1;words[2]=state->enabled;words[3]=request;
 words[4]=state->samples;words[5]=state->admitted;
 words[6]=state->submitted?state->trigger_percent:battery->percent;
 words[7]=state->submitted?state->trigger_mv:battery->millivolts;
 words[8]=(uint32_t)state->first_ns;words[9]=(uint32_t)(state->first_ns>>32);
 words[10]=(uint32_t)state->last_ns;words[11]=(uint32_t)(state->last_ns>>32);
 words[12]=(uint32_t)state->submitted_ns;words[13]=(uint32_t)(state->submitted_ns>>32);
 words[14]=I9100_LOW_BATTERY_SOC;words[15]=I9100_LOW_BATTERY_MV;
 words[16]=I9100_LOW_BATTERY_SAMPLES;words[17]=I9100_LOW_BATTERY_SECONDS;
 __sync_synchronize();words[1]=sequence;
}
#endif
