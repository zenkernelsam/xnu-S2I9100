.syntax unified
.thumb
.fpu neon
.text
.p2align 2
.global metrics_template
metrics_template:
    push.w {r0-r12, lr}
    mrs r12, apsr
    sub sp, #8
    str.w r12, [sp]
    movs r0, #1
    mov.w r12, #20
    svc #0x80
    ldr.w r12, [sp]
    add sp, #8
    msr apsr_nzcvqg, r12
    pop.w {r0-r12, lr}
    push.w {r0-r12, lr}
    mrs r12, apsr
    sub sp, #8
    str.w r12, [sp]
    vmrs r12, fpscr
    str.w r12, [sp, #4]
    vpush {d0-d1}
    ldr r0, [r4]
.global metrics_screen_class
metrics_screen_class:
    movw r1, #0
    movt r1, #0
    cmp r0, r1
    bne metrics_restore
    ldr r0, [r4, #24]
    mov.w r1, #0x3f800000
    cmp r0, r1
    bne metrics_restore
    ldr r0, [r4, #16]
    movw r1, #0
    movt r1, #0x43f0
    cmp r0, r1
    bne metrics_restore
    ldr r0, [r4, #20]
    movw r1, #0
    movt r1, #0x4448
    cmp r0, r1
    bne metrics_restore
.global metrics_pixel_state
metrics_pixel_state:
    movw r2, #0
    movt r2, #0
    ldr r0, [r2]
    cmp.w r0, #480
    bne metrics_restore
    ldr r0, [r2, #4]
    cmp.w r0, #800
    bne metrics_restore
.global metrics_gs_scale
metrics_gs_scale:
    movw r2, #0
    movt r2, #0
    ldr r0, [r2]
    mov.w r1, #0x3f800000
    cmp r0, r1
    bne metrics_restore
.global metrics_requested_scale
metrics_requested_scale:
    movw r12, #0
    movt r12, #0
    vmov s2, r12
    vldr s0, [r4, #16]
    vldr s1, [r4, #20]
    vdiv.f32 s0, s0, s2
    vdiv.f32 s1, s1, s2
    str.w r12, [r2]
    str.w r12, [r4, #24]
    vstr s0, [r4, #16]
    vstr s1, [r4, #20]
metrics_restore:
    vpop {d0-d1}
    ldr.w r12, [sp, #4]
    vmsr fpscr, r12
    ldr.w r12, [sp]
    add sp, #8
    msr apsr_nzcvqg, r12
    pop.w {r0-r12, lr}
.global metrics_after
metrics_after:
    push.w {r0-r12, lr}
    mrs r12, apsr
    sub sp, #8
    str.w r12, [sp]
    movs r0, #2
    mov.w r12, #20
    svc #0x80
    ldr.w r12, [sp]
    add sp, #8
    msr apsr_nzcvqg, r12
    pop.w {r0-r12, lr}
.global metrics_original
metrics_original:
    .byte 0x0a,0xb0,0xbd,0xec,0x02,0x8b,0xbd,0xe8,0x00,0x05,0xf0,0xbd
