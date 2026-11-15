/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_i2c_samsung.h"
#include <string.h>
enum {CON=0,STAT=4,ADD=8,DS=12,LC=16,ACK=0x80,IRQEN=0x20,PENDING=0x10,
      START=0x20,TXRX=0x10,ARBITRATION=8,NACK=1,MASTER_TX=0xc0,MASTER_RX=0x80};
static uint32_t rd(struct i9100_i2c_samsung *b,unsigned off){return b->io.read32(b->io.cookie,off);}
static void wr(struct i9100_i2c_samsung *b,unsigned off,uint32_t v){b->io.write32(b->io.cookie,off,v);}
static int elapsed(struct i9100_i2c_samsung *b)
{uint64_t n=b->io.now_ns(b->io.cookie);return n<b->started||n-b->started>b->timeout_ns;}
static int wait_pending(struct i9100_i2c_samsung *b,int allow_nack)
{
 for(unsigned p=0;p<b->poll_limit;p++) {
  uint32_t con=rd(b,CON),stat=rd(b,STAT);
  if(stat&ARBITRATION)return I9100_I2C_ARBITRATION;
  if(con&PENDING)return !allow_nack&&(stat&NACK)?I9100_I2C_DATA_NACK:0;
  if(elapsed(b))break;
  if(b->io.delay_us(b->io.cookie,2))return I9100_I2C_IO;
 }
 return I9100_I2C_TIMEOUT;
}
static int stop(struct i9100_i2c_samsung *b)
{
 wr(b,STAT,rd(b,STAT)&~START);
 wr(b,CON,b->con&~IRQEN);
 for(unsigned p=0;p<b->poll_limit;p++) {
  if(!(rd(b,STAT)&START))return 0;
  if(elapsed(b))break;
  if(b->io.delay_us(b->io.cookie,2))return I9100_I2C_IO;
 }
 return I9100_I2C_TIMEOUT;
}
int i9100_i2c_samsung_init(struct i9100_i2c_samsung *b,const struct i9100_i2c_samsung_io *io,uint32_t pclk,uint32_t hz)
{
 if(!b||b->initialized||!io||!io->read32||!io->write32||!io->lock||!io->unlock||
    !io->now_ns||!io->delay_us||pclk<1000000U||pclk>200000000U||hz<10000U||hz>100000U)
  return I9100_I2C_ARGUMENT;
 unsigned div=16U,scale=(pclk+(uint64_t)hz*16U-1U)/((uint64_t)hz*16U);
 if(scale>16U){div=512U;scale=(pclk+(uint64_t)hz*div-1U)/((uint64_t)hz*div);}
 if(!scale||scale>16U)return I9100_I2C_ARGUMENT;
 memset(b,0,sizeof(*b));b->io=*io;b->initialized=1;b->pclk_hz=pclk;
 b->bus_hz=pclk/(div*scale);b->con=ACK|IRQEN|(div==512U?64U:0U)|(scale-1U);
 b->timeout_ns=100000000ULL;b->poll_limit=50000U;return 0;
}
int i9100_i2c_samsung_enable(struct i9100_i2c_samsung *b,int enabled,int exclusive)
{
 if(!b||!b->initialized||(enabled!=0&&enabled!=1))return I9100_I2C_ARGUMENT;
 if(b->io.lock(b->io.cookie))return I9100_I2C_BUSY;
 int e=0;
 if(enabled){
  if(b->enabled||exclusive!=1)e=I9100_I2C_BUSY;
  else if((rd(b,STAT)&START)||(rd(b,CON)&PENDING))e=I9100_I2C_BUS;
  else {
   b->saved[0]=rd(b,CON);b->saved[1]=rd(b,STAT);b->saved[2]=rd(b,ADD);b->saved[3]=rd(b,LC);
   wr(b,CON,b->con&~IRQEN);wr(b,STAT,0);wr(b,LC,4U);
   b->enabled=b->exclusive=1;b->fault=0;
  }
 }else if(!b->enabled)e=I9100_I2C_ARGUMENT;
 else if(rd(b,STAT)&START)e=I9100_I2C_BUS;
 else {
  wr(b,STAT,0);wr(b,CON,0);wr(b,ADD,b->saved[2]);wr(b,LC,b->saved[3]);
  wr(b,STAT,b->saved[1]&~START);wr(b,CON,b->saved[0]&~PENDING);
  b->enabled=b->exclusive=b->fault=0;
 }
 b->io.unlock(b->io.cookie);return e;
}
int i9100_i2c_samsung_transfer(struct i9100_i2c_samsung *b,const struct i9100_i2c_message *m,unsigned count,unsigned *completed)
{
 if(completed)*completed=0;
 if(!b||!b->initialized||!m||!completed||!count||count>8U)return I9100_I2C_ARGUMENT;
 unsigned total=0;
 for(unsigned n=0;n<count;n++) {
  if(m[n].address<8U||m[n].address>0x77U||m[n].read>1U||!m[n].bytes||
     !m[n].length||m[n].length>4096U)return I9100_I2C_ARGUMENT;
  total+=m[n].length;
 }
 if(total>8192U)return I9100_I2C_ARGUMENT;
 if(b->io.lock(b->io.cookie))return I9100_I2C_BUSY;
 int e=0,owns_bus=0;
 if(!b->enabled||!b->exclusive){e=I9100_I2C_BUS;goto done;}
 if(b->fault){e=I9100_I2C_IO;goto done;}
 if(rd(b,STAT)&START){e=I9100_I2C_BUSY;goto done;}
 b->started=b->io.now_ns(b->io.cookie);
 for(unsigned n=0;n<count&&!e;n++) {
  unsigned mode=m[n].read?MASTER_RX:MASTER_TX;
  /* Keep a previous pending byte paused until the repeated START is loaded. */
  wr(b,STAT,mode|TXRX);wr(b,DS,((uint32_t)m[n].address<<1)|m[n].read);
  if(b->io.delay_us(b->io.cookie,1)){e=I9100_I2C_IO;break;}
  wr(b,CON,b->con|(n?PENDING:0U));wr(b,STAT,mode|TXRX|START);
  if(n)wr(b,CON,b->con);
  owns_bus=1;e=wait_pending(b,0);
  if(e==I9100_I2C_DATA_NACK)e=I9100_I2C_ADDRESS_NACK;
  for(unsigned j=0;j<m[n].length&&!e;j++) {
   if(!m[n].read)wr(b,DS,m[n].bytes[j]);
   /* Last read byte is NACKed before STOP or the next repeated START. */
   uint32_t con=b->con;
   if(m[n].read&&j+1U==m[n].length)con&=~ACK;
   wr(b,CON,con);e=wait_pending(b,m[n].read);
   if(!e&&m[n].read)m[n].bytes[j]=(uint8_t)rd(b,DS);
  }
  if(!e)(*completed)++;
 }
 if(e==I9100_I2C_ARBITRATION){wr(b,CON,b->con&~IRQEN);owns_bus=0;b->fault=1;}
 if(owns_bus){int end=stop(b);if(!e)e=end;}
 if(e==I9100_I2C_TIMEOUT||e==I9100_I2C_IO)b->fault=1;
done:
 b->io.unlock(b->io.cookie);return e;
}
