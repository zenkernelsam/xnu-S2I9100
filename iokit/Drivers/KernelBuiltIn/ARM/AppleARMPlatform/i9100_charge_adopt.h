#ifndef I9100_CHARGE_ADOPT_H
#define I9100_CHARGE_ADOPT_H
#include "i9100_power.h"
#include "i9100_charge_handoff.h"

enum {
 I9100_CHARGE_ADOPT_NONE=0,
 I9100_CHARGE_ADOPT_APPLIED=1,
 I9100_CHARGE_ADOPT_OFFLINE=2,
 I9100_CHARGE_ADOPT_REJECTED=3,
 I9100_CHARGE_ADOPT_EXPIRED=4
};

static inline uint64_t i9100_charge_ticks_ns(uint64_t ticks)
{
 const uint64_t maximum=~(uint64_t)0;
 uint64_t whole=ticks/24U,part=(ticks%24U)*1000U/24U;
 if(whole>(maximum-part)/1000U)return 0;
 return whole*1000U+part;
}

/* The v1 handoff retains elapsed whole seconds, not the exact start clock.
 * Round the inherited duration UP by one second: this can shorten a deadline,
 * never grant a new five-hour or ninety-minute charging period. Fresh kernel
 * GPIO/fuel/ADC reads still decide whether charging can actually resume. */
static inline unsigned i9100_charge_adopt(struct i9100_power_state *s,
 const void *raw,unsigned bytes,uint64_t now_ns)
{
 if(!raw && !bytes)return I9100_CHARGE_ADOPT_NONE;
 if(!s || !s->ready)return I9100_CHARGE_ADOPT_REJECTED;
 unsigned nonzero=0;
 if(!raw || bytes!=I9100_CHARGE_HANDOFF_BYTES)goto rejected;
 for(unsigned n=0;n<bytes;n++)nonzero|=((const uint8_t *)raw)[n];
 if(!nonzero)return I9100_CHARGE_ADOPT_NONE;
 {
  struct I9100ChargeHandoff r;
  memcpy(&r,raw,sizeof(r));
  if(r.magic!=I9100_CHARGE_HANDOFF_MAGIC || r.version!=1U ||
     r.bytes!=sizeof(r) || r.flags!=7U ||
     r.checksum!=i9100_charge_handoff_checksum(&r) ||
     r.revision!=s->revision || r.sub_charger!=s->use_sub ||
     r.revision>15U || r.sub_charger!=(r.revision>=8U) ||
     r.valid!=1U || r.present!=1U || r.connected>1U ||
     r.full>1U || r.timeout || r.recharging>1U ||
     r.enabled_before>1U || r.charging_before>r.enabled_before ||
     r.enabled_after || r.charging_after || r.fault || !r.polls ||
     r.qualified_samples!=11U || r.recovery_seconds<10U ||
     r.percent<3U || r.percent>100U || r.elapsed_seconds>86400U || r.millivolts<3600U ||
     r.millivolts>4300U || (int32_t)r.temperature_deci_c<50 ||
     (int32_t)r.temperature_deci_c>400 ||
     (r.full && (r.recharging || r.enabled_before ||
                  (int32_t)r.reason!=I9100_POWER_FULL)))goto rejected;
  uint64_t sampled=((uint64_t)r.sampled_high<<32)|r.sampled_low;
  uint64_t stopped=((uint64_t)r.stopped_high<<32)|r.stopped_low;
  uint64_t published=((uint64_t)r.published_high<<32)|r.published_low;
  uint64_t sampled_ns=i9100_charge_ticks_ns(sampled);
  uint64_t published_ns=i9100_charge_ticks_ns(published);
  if(!sampled_ns || !published_ns || sampled>stopped || stopped>published ||
     published-sampled>60000000ULL || now_ns<published_ns ||
     now_ns-published_ns>600000000000ULL ||
     (uint64_t)r.recovery_seconds>sampled_ns/1000000000ULL)goto rejected;
  if(!r.connected){
   if(r.enabled_before || r.charging_before || r.full || r.recharging ||
      r.elapsed_seconds || (int32_t)r.reason!=I9100_POWER_OFFLINE)goto rejected;
   return I9100_CHARGE_ADOPT_OFFLINE;
  }
  if(!r.full && (!r.enabled_before || (int32_t)r.reason!=I9100_POWER_OK))goto rejected;
  if(r.full && r.elapsed_seconds)goto rejected;
  s->full=r.full;s->recharging=r.recharging;s->recharge_low_count=0;
  if(!r.full){
   uint64_t elapsed=((uint64_t)r.elapsed_seconds+1U)*1000000000ULL;
   /* If the real start was in the first partial second, use the earliest
    * nonzero instant supported by the original policy's zero sentinel. */
   s->charge_start=elapsed<sampled_ns?sampled_ns-elapsed:1U;
   s->charge_elapsed_base=elapsed<sampled_ns?0U:elapsed-sampled_ns+1U;
   uint64_t limit=(s->recharging?5400ULL:18000ULL)*1000000000ULL;
   if(i9100_power_elapsed(s->charge_start,s->charge_elapsed_base,now_ns)>=limit){
    s->timeout=1;s->reason=I9100_POWER_TIMEOUT;
    return I9100_CHARGE_ADOPT_EXPIRED;
   }
  }
  return I9100_CHARGE_ADOPT_APPLIED;
 }
rejected:
 /* Init already performed a checked stop. A malformed nonempty handoff
  * cannot enable either controller; only a real later cable detach clears
  * this existing policy latch. Do not substitute stale voltage or SOC. */
 s->timeout=1;s->reason=I9100_POWER_TIMEOUT;
 return I9100_CHARGE_ADOPT_REJECTED;
}
#endif
