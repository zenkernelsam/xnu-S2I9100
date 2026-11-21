/* SPDX-License-Identifier: BSD-2-Clause
 * Explicit hardware response model. No physical supply or clock claim. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "i9100_mfc_power.h"
#include "i9100_mfc_host.h"
static struct model {
 uint32_t regs[MP_REGIONS][0x20000/4],locked,writes,reads,engine_reads,exclusive,borrowers,irq_leased;
 uint32_t drop_region,drop_offset,drop_count,drop_write,stuck_power,stuck_div,delay_error,freeze,flip_root;
 uint32_t log_region[48],log_offset[48];uint64_t ns;
} m;
static struct i9100_mfc_power p;
static uint32_t rd(void *c,unsigned region,unsigned off)
{
 (void)c;assert(region<MP_REGIONS&&off<0x20000&&!(off&3U));m.reads++;
 if(region==MP_MFC||region==MP_MMU_L||region==MP_MMU_R){
  assert((m.regs[MP_PMU][0x3c44/4]&7U)==7U&&(m.regs[MP_CMU][0xc928/4]&7U)==7U);m.engine_reads++;
 }
 uint32_t value=m.regs[region][off/4];if(m.flip_root&&region==MP_CMU&&off==0x14108){m.flip_root++;if(m.flip_root==3)value^=1U<<16;}
 return value;
}
static void wr(void *c,unsigned region,unsigned off,uint32_t v)
{
 (void)c;assert(m.locked&&region<MP_REGIONS&&m.writes<48);unsigned mask=region==MP_PMU?7U:off==0xc928?7U:off==0xc228?0x101U:off==0xc528?15U:0;
 assert(mask&&((v^m.regs[region][off/4])&~mask)==0);
 m.log_region[m.writes]=region;m.log_offset[m.writes]=off;m.writes++;
 if(m.drop_write==m.writes||(m.drop_count&&region==m.drop_region&&off==m.drop_offset)){if(m.drop_count)m.drop_count--;return;}
 m.regs[region][off/4]=v;
 if(region==MP_PMU&&off==0x3c40&&!m.stuck_power)m.regs[region][0x3c44/4]=(m.regs[region][0x3c44/4]&~7U)|(v&7U);
 if(region==MP_CMU&&off==0xc528&&m.stuck_div)m.regs[region][0xc628/4]=1;
}
static int lock(void *c){(void)c;if(m.locked)return -1;m.locked=1;return 0;}
static void unlock(void *c){(void)c;assert(m.locked);m.locked=0;}
static uint64_t now(void *c){(void)c;return m.ns;}
static int delay(void *c,uint32_t us){(void)c;assert(us==1U);if(!m.freeze)m.ns+=us*1000ULL;return m.delay_error?-1:0;}
static void barrier(void *c){(void)c;}
static int exclusive(void *c){(void)c;return m.exclusive?-1:0;}
static int consumers(void *c){(void)c;return m.borrowers?-1:0;}
static int irq_owned(void *c){(void)c;return m.irq_leased?1:0;}
static void setup(unsigned warm)
{
 assert(!p.owned);memset(&p,0,sizeof(p));memset(&m,0,sizeof(m));m.ns=1000000;
 m.regs[MP_CHIP][0]=0x43210011U;m.regs[MP_CMU][0x14200/4]=1U<<8;
 m.regs[MP_CMU][0x14108/4]=0xa0640301U; /* 24MHz*100/(3<<(1-1))=800MHz */
 m.regs[MP_CMU][0xc928/4]=0x80000000U|warm;m.regs[MP_CMU][0xc228/4]=0x8101U;m.regs[MP_CMU][0xc528/4]=0x3007U;
 m.regs[MP_PMU][0x3c40/4]=0x900U|(warm?7:0);m.regs[MP_PMU][0x3c44/4]=0x100U|(warm?7:0);
 struct i9100_mfc_power_io io={&m,rd,wr,lock,unlock,now,delay,barrier,exclusive,consumers,irq_owned};
 struct i9100_mfc_power_config cfg={1,24000000,200000000};assert(!i9100_mfc_power_init(&p,&io,&cfg));
 assert(!m.reads&&!m.writes);assert(i9100_mfc_power_acquire(&p)==I9100_MP_DISABLED);assert(!i9100_mfc_power_enable(&p,1));
}
static void done(void){int e=i9100_mfc_power_release(&p);if(e)fprintf(stderr,"release=%d power=%x/%x gate=%x src=%x div=%x\n",e,m.regs[MP_PMU][0x3c40/4],m.regs[MP_PMU][0x3c44/4],m.regs[MP_CMU][0xc928/4],m.regs[MP_CMU][0xc228/4],m.regs[MP_CMU][0xc528/4]);assert(!e&&!p.owned);}
static void parents(void)
{
 uint32_t hz,div,clock;
 assert(!i9100_mfc_power_parent(24000000,256,0xa0640301,200000000,&hz,&div,&clock)&&hz==800000000&&div==4&&clock==200000000);
 assert(!i9100_mfc_power_parent(24000000,256,0xa07d0301,180000000,&hz,&div,&clock)&&hz==1000000000&&div==6&&clock==166666666);
 assert(!i9100_mfc_power_parent(24000000,0,0,200000000,&hz,&div,&clock)&&hz==24000000&&div==1);
 assert(i9100_mfc_power_parent(24000000,256,0xa0640300,200000000,&hz,&div,&clock)==I9100_MP_CLOCK);
 assert(i9100_mfc_power_parent(24000000,256,0xa0640301,10000000,&hz,&div,&clock)==I9100_MP_CLOCK);
 assert(i9100_mfc_power_parent(26000000,256,0xa0640301,200000000,&hz,&div,&clock)==I9100_MP_ARGUMENT);
}
static void cold_warm(void)
{
 for(unsigned warm=0;warm<=7;warm+=7){setup(warm);assert(!i9100_mfc_power_acquire(&p)&&p.ready&&p.parent_hz==800000000&&p.clock_hz==200000000);
  if(!warm)assert(m.log_region[0]==MP_CMU&&m.log_offset[0]==0xc928&&m.log_region[1]==MP_PMU&&m.log_offset[1]==0x3c40);
  assert(!i9100_mfc_power_validate(&p));m.borrowers=1;unsigned writes=m.writes;
  assert(i9100_mfc_power_release(&p)==I9100_MP_BUSY&&p.owned&&m.writes==writes);m.borrowers=0;done();
  assert(m.regs[MP_PMU][0x3c40/4]==(0x900U|(warm?7:0))&&m.regs[MP_CMU][0xc928/4]==(0x80000000U|warm));
  assert(m.regs[MP_CMU][0xc228/4]==0x8101U&&m.regs[MP_CMU][0xc528/4]==0x3007U);
 }
}
static void failures(void)
{
 setup(0);m.regs[MP_CHIP][0]=0x43220000;assert(i9100_mfc_power_acquire(&p)==I9100_MP_OWNER&&!p.owned&&!m.writes);
 setup(0);m.regs[MP_GIC][0x10c/4]=1U<<30;assert(i9100_mfc_power_acquire(&p)==I9100_MP_BUSY&&!p.owned&&!m.writes);
 setup(0);m.regs[MP_GIC][0x30c/4]=1U<<30;assert(i9100_mfc_power_acquire(&p)==I9100_MP_BUSY&&!m.writes);
 setup(0);m.regs[MP_PMU][0x3c44/4]=3;assert(i9100_mfc_power_acquire(&p)==I9100_MP_BUSY&&!m.writes);
 setup(0);m.exclusive=1;assert(i9100_mfc_power_acquire(&p)==I9100_MP_OWNER&&!m.reads);
 setup(0);m.flip_root=1;assert(i9100_mfc_power_acquire(&p)==I9100_MP_OWNER&&!m.writes);
 for(unsigned n=1;n<=5;n++){setup(0);m.drop_write=n;assert(i9100_mfc_power_acquire(&p)==I9100_MP_IO&&p.owned);m.drop_write=0;done();}
 setup(3);m.drop_write=1;assert(i9100_mfc_power_acquire(&p)==I9100_MP_IO&&!m.engine_reads);m.drop_write=0;
 done();assert(!m.engine_reads&&(m.regs[MP_CMU][0xc928/4]&7U)==3U&&(m.regs[MP_PMU][0x3c44/4]&7U)==7U);
 setup(0);m.stuck_power=m.freeze=1;assert(i9100_mfc_power_acquire(&p)==I9100_MP_TIMEOUT&&p.owned&&!m.engine_reads);
 m.regs[MP_PMU][0x3c44/4]|=7;m.stuck_power=m.freeze=0;done();
 setup(0);m.stuck_power=m.delay_error=1;assert(i9100_mfc_power_acquire(&p)==I9100_MP_IO&&p.owned);m.stuck_power=m.delay_error=0;m.regs[MP_PMU][0x3c44/4]|=7;done();
 setup(7);m.regs[MP_MFC][0]=0x3ff;assert(i9100_mfc_power_acquire(&p)==I9100_MP_BUSY&&p.owned&&!p.source_written&&!p.divider_written);
 assert(i9100_mfc_power_release(&p)==I9100_MP_BUSY);m.regs[MP_MFC][0]=0;done();
 setup(0);assert(!i9100_mfc_power_acquire(&p));m.regs[MP_CMU][0x14108/4]^=1U<<16;
 assert(i9100_mfc_power_validate(&p)==I9100_MP_CLOCK&&p.fault);m.regs[MP_CMU][0x14108/4]^=1U<<16;assert(!i9100_mfc_power_validate(&p));done();
 setup(0);assert(!i9100_mfc_power_acquire(&p));m.regs[MP_CMU][0xc228/4]=0x8100;
 assert(i9100_mfc_power_release(&p)==I9100_MP_OWNER&&p.owned);m.regs[MP_CMU][0xc228/4]=0x8000;done();
 setup(0);assert(!i9100_mfc_power_acquire(&p));m.drop_region=MP_PMU;m.drop_offset=0x3c40;m.drop_count=1;
 assert(i9100_mfc_power_release(&p)==I9100_MP_IO&&p.owned);done();
 setup(0);m.stuck_div=1;assert(i9100_mfc_power_acquire(&p)==I9100_MP_TIMEOUT&&p.owned);m.stuck_div=0;m.regs[MP_CMU][0xc628/4]=0;done();
 setup(0);assert(!i9100_mfc_power_acquire(&p));m.regs[MP_GIC][0x10c/4]=1U<<30;m.regs[MP_GIC][0x30c/4]=1U<<30;
 assert(i9100_mfc_power_validate(&p)==I9100_MP_OWNER&&p.fault);m.irq_leased=1;
 assert(!i9100_mfc_power_validate(&p));m.irq_leased=0;done();
}
static void owners(void)
{
 struct i9100_mfc_host_lease a={0},b={0},c={0};assert(!i9100_mfc_host_acquire(0,&a));
 assert(i9100_mfc_host_acquire(0,&b)==I9100_MP_BUSY);assert(!i9100_mfc_host_acquire(&a,&b));
 assert(i9100_mfc_host_has_child(&a)==1&&i9100_mfc_host_release(&a,1)==I9100_MP_BUSY);
 assert(i9100_mfc_host_begin_close(&a)==I9100_MP_BUSY);
 assert(i9100_mfc_host_acquire(&a,&c)==I9100_MP_BUSY);c=b;assert(i9100_mfc_host_check(&c)==I9100_MP_OWNER);
 assert(i9100_mfc_host_release(&b,0)==I9100_MP_BUSY);assert(!i9100_mfc_host_release(&b,1));
 memset(&b,0,sizeof(b));assert(!i9100_mfc_host_begin_close(&a));assert(i9100_mfc_host_acquire(&a,&b)==I9100_MP_BUSY);
 assert(!i9100_mfc_host_release(&a,1));
 memset(&c,0,sizeof(c));assert(!i9100_mfc_host_acquire(0,&c));assert(i9100_mfc_host_check(&a)==I9100_MP_OWNER);assert(!i9100_mfc_host_release(&c,1));
}
int main(void){parents();owners();cold_warm();failures();puts("PASS actual MFC power/root+child ownership with explicit PMU/CMU/MMU/clock model: cold/warm local gates, MPLL45 divider, no gated reads, IRQ/foreign/live refusal, drop/timeout/frozen/partial cleanup and retained borrowers; hardware=false");return 0;}
