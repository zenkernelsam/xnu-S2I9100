.syntax unified
.thumb
.text
.global hid_trace_prefix
.type hid_trace_prefix,%function
.thumb_func
hid_trace_prefix:
    push.w {r0-r12,lr}
    mrs r12,apsr
    sub sp,#8
    str r12,[sp]
    movs r0,#1
    mov.w r12,#20
    svc #0x80
    ldr r12,[sp]
    add sp,#8
    msr apsr_nzcvqg,r12
    pop.w {r0-r12,lr}
