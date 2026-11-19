#!/usr/bin/env python3
"""Apply IRQ routing patch only to an out copy and exercise its exact IAR/EOI handler."""
import hashlib
import shutil
from pathlib import Path
from i9100_offline_build import Evidence, REPO

e = Evidence("irq-dispatch-patch-offline")
tree = e.out / "tree"
source = REPO / "pexpert/arm/i9100_timer.c"
patch = REPO / "port-tests/i9100_peripheral_irq_dispatch.patch"
target = tree / "pexpert/arm/i9100_timer.c"
target.parent.mkdir(parents=True)
shutil.copyfile(source, target)
original = source.read_bytes()
e.run(["git", "apply", "--directory=" + str(tree.relative_to(REPO)), str(patch)], "apply-out-copy", cwd=REPO)
text = target.read_text()
start = text.index("void I9100_handle_interrupt(void *context)")
brace = text.index("{", start)
depth = 1
end = brace + 1
while depth:
    depth += (text[end] == "{") - (text[end] == "}")
    end += 1
handler = text[start:end]
prefix = r'''
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
typedef struct {uint32_t pc;} arm_saved_state_t;
enum {I9100_GIC_CPU_INTACK=12,I9100_GIC_CPU_EOI=16,I9100_MCT_G_INT_CSTAT=0x244,I9100_TIMER_IRQ=89};
static int controller_ready=1;
static unsigned i9100_gic_cpu=1,i9100_mct=2;
static uint32_t last_vector,spurious_irq_count,irq_count,last_count_lo,last_irq_counter;
static unsigned reads,eois,routes,ticks,observes,polls;
static uint32_t ack_value,pending_value,route_token,eoi_token;
static uint32_t read_reg(unsigned bank,unsigned off){
 if(bank==i9100_gic_cpu){assert(off==I9100_GIC_CPU_INTACK);reads++;return ack_value;}
 assert(bank==i9100_mct&&off==I9100_MCT_G_INT_CSTAT);return pending_value;
}
static void write_reg(unsigned bank,unsigned off,uint32_t value){
 if(bank==i9100_gic_cpu){assert(off==I9100_GIC_CPU_EOI);eois++;eoi_token=value;}
 else{assert(bank==i9100_mct&&off==I9100_MCT_G_INT_CSTAT&&value==1);pending_value=0;}
}
static int i9100_irq_route(uint32_t ack){assert(!eois);routes++;route_token=ack;return 1;}
static uint64_t mct_get_frc(void){return 42;}
static void drain(void){}
static void ios7i9100_timer_observe(uint32_t a,uint32_t b,uint32_t c,uint32_t d){(void)a;(void)b;(void)c;(void)d;observes++;}
static void i9100_usbcdc_poll(void){polls++;}
static void rtclock_intr(arm_saved_state_t *s){(void)s;ticks++;}
'''
suffix = r'''
int main(void){
 arm_saved_state_t state={123};
 const unsigned interrupts[]={67,89,200,1023};
 for(unsigned n=0;n<4;n++)for(unsigned pending=0;pending<2;pending++){
  reads=eois=routes=ticks=observes=polls=0;
  ack_value=interrupts[n]|(3U<<10);pending_value=pending;
  I9100_handle_interrupt(&state);assert(reads==1);
  if(interrupts[n]==1023){assert(!eois&&!routes&&!ticks);continue;}
  assert(eois==1&&eoi_token==ack_value);
  assert(routes==(interrupts[n]!=89));if(routes)assert(route_token==ack_value);
  assert(ticks==(interrupts[n]==89||pending));assert(observes==ticks&&polls==ticks);
 }
 puts("PASS exact patched handler: one IAR/full-token route-before-MCT/one original EOI/spurious IRQ/no mainline mutation");
 return 0;
}
'''
model = e.out / "handler-model.c"
model.write_text(prefix + handler + suffix)
e.run(["clang", "-std=c99", "-O1", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
       "-fno-sanitize-recover=all", "-fno-pie", "-no-pie", str(model), "-o", str(e.out / "models")], "compile-handler-model")
e.run([str(e.out / "models")], "handler-tests")
if source.read_bytes() != original:
    raise RuntimeError("Mainline timer changed during verification")
e.result.update(patch_sha256=hashlib.sha256(patch.read_bytes()).hexdigest(),
                timer_sha256=hashlib.sha256(original).hexdigest(), exact_handler_sha256=hashlib.sha256(handler.encode()).hexdigest(),
                patch_applied_to_mainline=False)
e.finish([], "Exact out-copy IRQ handler contract; no ARM/GIC timing or live interrupt verification")
