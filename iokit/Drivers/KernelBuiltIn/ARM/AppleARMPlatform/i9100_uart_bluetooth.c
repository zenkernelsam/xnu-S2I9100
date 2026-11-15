/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_uart_bluetooth.h"
#include <string.h>
enum {LCON=0,UCON=4,FCON=8,MCON=12,TRSTATUS=16,ERROR=20,FSTATUS=24,
 TX=32,RX=36,BAUD_DIV=40,FRACTION=44,PENDING=48,INTMASK=56};
static uint32_t rd(struct i9100_uart_bluetooth *u,unsigned off){return u->io.read32(u->io.cookie,off);}
static void wr(struct i9100_uart_bluetooth *u,unsigned off,uint32_t value){u->io.write32(u->io.cookie,off,value);}
static int wait_empty(struct i9100_uart_bluetooth *u)
{
 uint64_t start=u->io.now_ns(u->io.cookie);
 for(unsigned n=0;n<10000U;n++){
  if(rd(u,TRSTATUS)&4U)return 0;
  uint64_t now=u->io.now_ns(u->io.cookie);if(now<start||now-start>100000000ULL)break;
  if(u->io.delay_us(u->io.cookie,10))return I9100_UART_IO;
 }
 return I9100_UART_TIMEOUT;
}
int i9100_uart_bt_init(struct i9100_uart_bluetooth *u,const struct i9100_uart_io *io)
{
 if(!u||u->initialized||!io||!io->read32||!io->read8||!io->write32||!io->write8||!io->lock||!io->unlock||!io->now_ns||!io->delay_us)return I9100_UART_ARGUMENT;
 memset(u,0,sizeof(*u));u->io=*io;u->initialized=1;return 0;
}
int i9100_uart_bt_enable(struct i9100_uart_bluetooth *u,int enabled)
{
 if(!u||!u->initialized||(enabled!=0&&enabled!=1))return I9100_UART_ARGUMENT;
 if(u->io.lock(u->io.cookie))return I9100_UART_BUSY;
 int e=u->prepared||u->fault?I9100_UART_BUSY:0;if(!e)u->enabled=enabled;u->io.unlock(u->io.cookie);return e;
}
int i9100_uart_bt_configure(struct i9100_uart_bluetooth *u,uint32_t clock,uint32_t baud,enum i9100_uart_fraction mode,int verified)
{
 static const uint16_t slots[16]={0x0000,0x0080,0x0808,0x0888,0x2222,0x4924,0x4a52,0x54aa,0x5555,0xd555,0xd5d5,0xddd5,0xdddd,0xdfdd,0xdfdf,0xffdf};
 if(!u||!u->initialized||!verified||clock<1000000U||clock>200000000U||
    (baud!=115200U&&baud!=921600U&&baud!=3000000U)||(mode!=1&&mode!=2))return I9100_UART_ARGUMENT;
 uint32_t total=(clock+baud/2U)/baud,quotient=total/16U;
 if(!quotient||quotient>65536U)return I9100_UART_ARGUMENT;
 uint32_t actual=clock/total,difference=actual>baud?actual-baud:baud-actual;
 if((uint64_t)difference*1000U>(uint64_t)baud*20U)return I9100_UART_ARGUMENT;
 if(u->io.lock(u->io.cookie))return I9100_UART_BUSY;
 int e=0;if(!u->enabled){e=I9100_UART_DISABLED;goto done;}if(u->prepared||u->fault){e=I9100_UART_BUSY;goto done;}
 uint32_t status=rd(u,FSTATUS),control=rd(u,UCON);
 if((status&511U)||!(rd(u,TRSTATUS)&4U)||(rd(u,INTMASK)&15U)!=15U||
    (control&3U)>1U||((control>>2)&3U)>1U){e=I9100_UART_BUSY;goto done;}
 const unsigned regs[7]={LCON,UCON,FCON,MCON,BAUD_DIV,FRACTION,INTMASK};
 for(unsigned n=0;n<7;n++)u->saved[n]=rd(u,regs[n]);
 u->prepared=1;u->clock_hz=clock;u->baud=baud;u->actual_baud=actual;u->fraction_mode=mode;
 uint32_t fraction=mode==I9100_UART_RAW_FRACTION?total%16U:slots[total%16U];
 wr(u,UCON,control&0x400U);wr(u,INTMASK,15);wr(u,LCON,3);wr(u,BAUD_DIV,quotient-1U);
 wr(u,FRACTION,fraction);wr(u,MCON,16U);wr(u,FCON,7U);wr(u,FCON,1U);
 wr(u,PENDING,15U);wr(u,UCON,(control&0x400U)|5U);
 if(rd(u,LCON)!=3U||rd(u,BAUD_DIV)!=quotient-1U||rd(u,FRACTION)!=fraction||!(rd(u,MCON)&16U)||
    rd(u,UCON)!=((control&0x400U)|5U)||(rd(u,FCON)&7U)!=1U||(rd(u,INTMASK)&15U)!=15U)
  {e=I9100_UART_IO;u->fault=1;}
done:
 u->io.unlock(u->io.cookie);return e;
}
int i9100_uart_bt_write(struct i9100_uart_bluetooth *u,const uint8_t *bytes,unsigned count,unsigned *sent)
{
 if(sent)*sent=0;
 if(!u||!u->initialized||!bytes||!count||count>4101U||!sent)return I9100_UART_ARGUMENT;
 if(u->io.lock(u->io.cookie))return I9100_UART_BUSY;
 int e=0;if(!u->enabled||!u->prepared){e=I9100_UART_DISABLED;goto done;}if(u->fault){e=I9100_UART_IO;goto done;}
 uint64_t start=u->io.now_ns(u->io.cookie);
 uint64_t allowance=((uint64_t)count*10000000000ULL)/u->actual_baud+100000000ULL;
 unsigned polls=0;
 while(*sent<count){
  if(!(rd(u,FSTATUS)&(1U<<24))){u->io.write8(u->io.cookie,TX,bytes[*sent]);(*sent)++;continue;}
  uint64_t now=u->io.now_ns(u->io.cookie);
  if(now<start||now-start>allowance||++polls>100000U){e=I9100_UART_TIMEOUT;break;}
  if(u->io.delay_us(u->io.cookie,10)){e=I9100_UART_IO;break;}
 }
 u->tx_bytes=UINT32_MAX-u->tx_bytes<*sent?UINT32_MAX:u->tx_bytes+*sent;if(e)u->fault=1;
done:
 u->io.unlock(u->io.cookie);return e;
}
int i9100_uart_bt_read(struct i9100_uart_bluetooth *u,uint8_t *bytes,unsigned capacity,unsigned *received)
{
 if(received)*received=0;
 if(!u||!u->initialized||!bytes||!capacity||capacity>4101U||!received)return I9100_UART_ARGUMENT;
 if(u->io.lock(u->io.cookie))return I9100_UART_BUSY;
 int e=0;if(!u->enabled||!u->prepared){e=I9100_UART_DISABLED;goto done;}if(u->fault){e=I9100_UART_IO;goto done;}
 while(*received<capacity){
  uint32_t status=rd(u,FSTATUS);if(!(status&511U))break;
  if(rd(u,ERROR)&15U){u->rx_errors++;u->fault=1;e=I9100_UART_RX_ERROR;break;}
  bytes[(*received)++]=u->io.read8(u->io.cookie,RX);
 }
done:
 u->io.unlock(u->io.cookie);return e;
}
int i9100_uart_bt_stop(struct i9100_uart_bluetooth *u)
{
 if(!u||!u->initialized)return I9100_UART_ARGUMENT;
 if(u->io.lock(u->io.cookie))return I9100_UART_BUSY;
 int e=0;if(!u->prepared){e=I9100_UART_DISABLED;goto done;}
 e=wait_empty(u);if(e){u->fault=1;goto done;}
 wr(u,UCON,rd(u,UCON)&0x400U);
 const unsigned regs[7]={LCON,UCON,FCON,MCON,BAUD_DIV,FRACTION,INTMASK};
 /* Restore disabled directions last; pending RX bytes belong to this stopped
  * generation and must be dealt with by the peer-reset/owner handoff. */
 for(unsigned n=0;n<7;n++)if(n!=1U)wr(u,regs[n],u->saved[n]);
 wr(u,UCON,u->saved[1]);
 for(unsigned n=0;n<7;n++)if(rd(u,regs[n])!=u->saved[n]){e=I9100_UART_IO;u->fault=1;}
 if(!e)u->prepared=0; /* fault remains until a fresh instance */
done:
 u->io.unlock(u->io.cookie);return e;
}
