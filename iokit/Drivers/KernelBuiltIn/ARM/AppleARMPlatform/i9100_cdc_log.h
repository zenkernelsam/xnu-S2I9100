#ifndef I9100_CDC_LOG_H
#define I9100_CDC_LOG_H
#include <stdint.h>
#include <string.h>
#include "../../../../../pexpert/arm/i9100_usb_stats.h"

#define I9100_CDC_LOG_PERIOD_NS 15000000000ULL
#define I9100_CDC_LOG_STALL_NS 3000000000ULL
#define I9100_CDC_LOG_DIAG_WORDS (8U+I9100_USB_STATS_WORDS)
enum {I9CL_FIRST=1U,I9CL_STATE=2U,I9CL_FAULT=4U,I9CL_PERIOD=8U,I9CL_CLOCK=16U};
enum {I9CF_INIT=1U,I9CF_RESET=2U,I9CF_ENDPOINT=4U,I9CF_POLL=8U,I9CF_REPLY=16U};
struct i9100_cdc_log_state {
 uint32_t initialized,key[10],faults,last_poll,emitted,suppressed;
 uint64_t last_ns,last_emit_ns,poll_progress_ns;
};

/* Normal poll/rx/tx progress, busy, DATA0/1 and idle OUT re-arms are not
 * failures. Preserve all 27 raw words separately; emit immediately only on
 * identity/connection/control gates or fault transitions, plus a real-time
 * heartbeat. This helper neither polls hardware nor changes USB behavior. */
static inline uint32_t i9100_cdc_log_decide(struct i9100_cdc_log_state *s,
 const uint32_t raw[I9100_USB_STATS_WORDS],uint64_t now_ns)
{
 const uint32_t endpoint_mask=(1U<<30)|(1U<<21)|(1U<<15)|0x7ffU;
 uint32_t key[10]={raw[0],raw[8],raw[9],raw[16],raw[21],raw[23],raw[25],
  raw[15]&endpoint_mask,raw[18]&endpoint_mask,raw[26]};
 uint32_t reason=0;
 unsigned healthy=raw[8] && raw[9] && raw[21] && raw[26];
 if(!s->initialized){
  s->initialized=1;reason|=I9CL_FIRST;s->poll_progress_ns=now_ns;
 }else{
  if(memcmp(s->key,key,sizeof(key)))reason|=I9CL_STATE;
  if(now_ns<s->last_ns || (!s->last_ns && now_ns)){
   reason|=I9CL_CLOCK;s->poll_progress_ns=now_ns;
  }
 }
 if(!healthy || !now_ns || raw[2]!=s->last_poll || now_ns<s->poll_progress_ns)
  s->poll_progress_ns=now_ns;
 uint32_t faults=(raw[23]?I9CF_INIT:0U)|(raw[25]?I9CF_RESET:0U);
 if(healthy){
  if((raw[15]|raw[18])&((1U<<30)|(1U<<21)))faults|=I9CF_ENDPOINT;
  if(now_ns && now_ns>=s->poll_progress_ns &&
     now_ns-s->poll_progress_ns>=I9100_CDC_LOG_STALL_NS)faults|=I9CF_POLL;
  if((raw[10] || raw[11]) && raw[17]>=3U)faults|=I9CF_REPLY;
 }
 if(faults!=s->faults)reason|=I9CL_FAULT;
 if(now_ns && now_ns>=s->last_emit_ns &&
    now_ns-s->last_emit_ns>=I9100_CDC_LOG_PERIOD_NS)reason|=I9CL_PERIOD;
 memcpy(s->key,key,sizeof(key));s->faults=faults;s->last_poll=raw[2];s->last_ns=now_ns;
 if(reason){s->emitted++;s->last_emit_ns=now_ns;}else s->suppressed++;
 return reason;
}

/* Single Power-workloop writer. Odd/even publication can be retained as
 * partial by a host; never wait, allocate or format from an IRQ. */
static inline void i9100_cdc_log_publish(volatile uint32_t diag[I9100_CDC_LOG_DIAG_WORDS],
 const struct i9100_cdc_log_state *s,const uint32_t raw[I9100_USB_STATS_WORDS],
 uint64_t now_ns,uint32_t reason)
{
 diag[1]++;__sync_synchronize();
 diag[0]=1;diag[2]=I9100_USB_STATS_WORDS;diag[3]=s->emitted;diag[4]=s->suppressed;
 diag[5]=reason|(s->faults<<8);diag[6]=(uint32_t)now_ns;diag[7]=(uint32_t)(now_ns>>32);
 for(unsigned n=0;n<I9100_USB_STATS_WORDS;n++)diag[8+n]=raw[n];
 __sync_synchronize();diag[1]++;__sync_synchronize();
}
#endif
