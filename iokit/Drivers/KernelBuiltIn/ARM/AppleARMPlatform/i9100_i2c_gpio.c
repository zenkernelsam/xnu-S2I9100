/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_i2c_gpio.h"
#include <string.h>
static int low(struct i9100_i2c_gpio *b,unsigned pin){return i9100_gpio_output(&b->lease,pin,0);}
static int release(struct i9100_i2c_gpio *b,unsigned pin){return i9100_gpio_input(&b->lease,pin);}
static int sample(struct i9100_i2c_gpio *b,unsigned pin,unsigned *v){return i9100_gpio_level(&b->lease,pin,v);}
static int time_ok(struct i9100_i2c_gpio *b)
{uint64_t n=b->timing.now_ns(b->timing.cookie);return n<b->started||n-b->started>=b->timing.transfer_ns?I9100_I2C_TIMEOUT:0;}
static int pause(struct i9100_i2c_gpio *b)
{if(b->timing.delay_us(b->timing.cookie,b->timing.half_us))return I9100_I2C_IO;return time_ok(b);}
static int enter(struct i9100_i2c_gpio *b)
{
 if(!b||!b->valid||!b->lease.active)return I9100_I2C_ARGUMENT;
 struct i9100_gpio_broker *r=b->lease.broker;
 if(r->io.lock(r->io.cookie))return I9100_I2C_BUSY;
 if(b->busy){r->io.unlock(r->io.cookie);return I9100_I2C_BUSY;}
 b->busy=1;r->io.unlock(r->io.cookie);b->started=b->timing.now_ns(b->timing.cookie);return 0;
}
static int leave(struct i9100_i2c_gpio *b)
{
 struct i9100_gpio_broker *r=b->lease.broker;
 /* Busy clearing must not be lost on a try-lock failure. The board adapter
  * supplies a normal blocking thread mutex; callbacks never run in IRQ. */
 if(r->io.lock(r->io.cookie))return I9100_I2C_BUSY;
 b->busy=0;r->io.unlock(r->io.cookie);return 0;
}
static int high_clock(struct i9100_i2c_gpio *b)
{
 int e=release(b,b->scl);if(e)return e;uint64_t start=b->timing.now_ns(b->timing.cookie);
 for(unsigned n=0;n<b->timing.poll_bound;n++){
  unsigned value;e=sample(b,b->scl,&value);if(e)return e;if(value)return pause(b);
  uint64_t now=b->timing.now_ns(b->timing.cookie);
  if(now<start||now-start>=b->timing.stretch_ns||time_ok(b))return I9100_I2C_TIMEOUT;
  if(b->timing.delay_us(b->timing.cookie,1))return I9100_I2C_IO;
 }
 return I9100_I2C_TIMEOUT;
}
static int start(struct i9100_i2c_gpio *b)
{
 int e=release(b,b->sda);if(e)return e;e=pause(b);if(e)return e;
 e=high_clock(b);if(e)return e;unsigned data;e=sample(b,b->sda,&data);if(e)return e;
 if(!data)return I9100_I2C_BUS;
 e=low(b,b->sda);if(e)return e;e=pause(b);if(e)return e;
 e=low(b,b->scl);return e?e:pause(b);
}
static int stop(struct i9100_i2c_gpio *b)
{
 int e=low(b,b->scl);if(e)return e;e=low(b,b->sda);if(e)return e;e=pause(b);if(e)return e;
 e=high_clock(b);if(e)return e;e=release(b,b->sda);return e?e:pause(b);
}
static int write_bit(struct i9100_i2c_gpio *b,unsigned value,int arbitrate)
{
 int e=value?release(b,b->sda):low(b,b->sda);if(e)return e;e=pause(b);if(e)return e;
 e=high_clock(b);if(e)return e;
 if(value&&arbitrate){unsigned data;e=sample(b,b->sda,&data);if(e)return e;if(!data)return I9100_I2C_ARBITRATION;}
 e=low(b,b->scl);return e?e:pause(b);
}
static int read_bit(struct i9100_i2c_gpio *b,unsigned *value)
{
 int e=release(b,b->sda);if(e)return e;e=pause(b);if(e)return e;
 e=high_clock(b);if(e)return e;e=sample(b,b->sda,value);if(e)return e;
 e=low(b,b->scl);return e?e:pause(b);
}
static int send(struct i9100_i2c_gpio *b,uint8_t byte,int address)
{
 for(unsigned n=0;n<8;n++){int e=write_bit(b,(byte>>(7U-n))&1U,1);if(e)return e;}
 unsigned ack;int e=read_bit(b,&ack);if(e)return e;
 return ack?(address?I9100_I2C_ADDRESS_NACK:I9100_I2C_DATA_NACK):0;
}
static int receive(struct i9100_i2c_gpio *b,uint8_t *byte,int last)
{
 unsigned value=0;
 for(unsigned n=0;n<8;n++){unsigned bit;int e=read_bit(b,&bit);if(e)return e;value=(value<<1)|bit;}
 int e=write_bit(b,last?1U:0U,0);if(e)return e;*byte=(uint8_t)value;return release(b,b->sda);
}
int i9100_i2c_gpio_init(struct i9100_i2c_gpio *b,struct i9100_gpio_broker *r,unsigned bank,unsigned sda,unsigned scl,const struct i9100_i2c_timing *t)
{
 if(!b||!r||!t||b->valid||!t->now_ns||!t->delay_us||!t->half_us||t->half_us>1000U||
    !t->poll_bound||t->poll_bound>100000U||!t->stretch_ns||!t->transfer_ns||t->stretch_ns>t->transfer_ns||
    bank>=r->count||sda>=r->banks[bank].config.pins||scl>=r->banks[bank].config.pins||scl==sda)return I9100_I2C_ARGUMENT;
 memset(b,0,sizeof(*b));b->timing=*t;b->sda=sda;b->scl=scl;
 int e=i9100_gpio_claim(r,bank,(1U<<sda)|(1U<<scl),&b->lease);if(e)return e;b->valid=1;return 0;
}
int i9100_i2c_gpio_transfer(struct i9100_i2c_gpio *b,const struct i9100_i2c_message *m,unsigned count,unsigned *completed)
{
 if(completed)*completed=0;
 if(!m||!count||count>8U||!completed)return I9100_I2C_ARGUMENT;
 unsigned total=0;
 for(unsigned n=0;n<count;n++){
  if(m[n].address<8U||m[n].address>0x77U||m[n].read>1||!m[n].bytes||!m[n].length||m[n].length>256U)return I9100_I2C_ARGUMENT;
  total+=m[n].length;
 }if(total>1024U)return I9100_I2C_ARGUMENT;
 int e=enter(b);if(e)return e;
 e=i9100_gpio_pull(&b->lease,b->sda,0);if(!e)e=i9100_gpio_pull(&b->lease,b->scl,0);
 if(e){int done=leave(b);return done?done:e;}
 for(unsigned n=0;n<count;n++){
  e=start(b);if(e)break;e=send(b,(uint8_t)(m[n].address<<1)|m[n].read,1);if(e)break;
  for(unsigned j=0;j<m[n].length;j++){
   e=m[n].read?receive(b,m[n].bytes+j,j+1U==m[n].length):send(b,m[n].bytes[j],0);if(e)break;
  }if(e)break;(*completed)++;
 }
 if(e!=I9100_I2C_ARBITRATION){int s=stop(b);if(!e)e=s;}
 release(b,b->sda);release(b,b->scl);int done=leave(b);return e?e:done;
}
int i9100_i2c_gpio_recover(struct i9100_i2c_gpio *b)
{
 int e=enter(b);if(e)return e;e=release(b,b->sda);
 for(unsigned n=0;!e&&n<9;n++){e=low(b,b->scl);if(!e)e=pause(b);if(!e)e=high_clock(b);}
 if(!e)e=stop(b);
 unsigned value=0;if(!e)e=sample(b,b->sda,&value);if(!e&&!value)e=I9100_I2C_BUS;
 release(b,b->sda);release(b,b->scl);int done=leave(b);return e?e:done;
}
int i9100_i2c_gpio_destroy(struct i9100_i2c_gpio *b)
{
 if(!b||!b->valid)return I9100_I2C_ARGUMENT;
 struct i9100_gpio_broker *r=b->lease.broker;if(r->io.lock(r->io.cookie))return I9100_I2C_BUSY;
 if(b->busy){r->io.unlock(r->io.cookie);return I9100_I2C_BUSY;}
 b->valid=0;r->io.unlock(r->io.cookie);int e=i9100_gpio_release(&b->lease,I9100_GPIO_RESTORE);if(e)b->valid=1;return e;
}
