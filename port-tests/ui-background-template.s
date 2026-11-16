.syntax unified
.thumb
.fpu neon
.text
.p2align 2
.global bg_template
bg_template:
    push.w {r0-r12, lr}
    mrs r12, apsr
    sub sp, #8
    str.w r12, [sp]
    cbz r0, bg_restore
    cmp.w r8, #0
    beq bg_restore
    vpush {d0-d7}
    vpush {d16-d31}
    sub sp, #32
    vmrs r12, fpscr
    str.w r12, [sp, #24]
    add r0, sp, #8
    mov r1, r8
.global bg_bounds_selector
bg_bounds_selector:
    movw r2, #0
    movt r2, #0
.global bg_bounds_imp
bg_bounds_imp:
    movw r12, #0
    movt r12, #0
    blx r12
    ldr r0, [sp, #232]
.global bg_frame_selector
bg_frame_selector:
    movw r1, #0
    movt r1, #0
    ldr r2, [sp, #8]
    ldr r3, [sp, #12]
    ldr r12, [sp, #16]
    str r12, [sp]
    ldr r12, [sp, #20]
    str r12, [sp, #4]
.global bg_frame_imp
bg_frame_imp:
    movw r12, #0
    movt r12, #0
    blx r12
    ldr r0, [sp, #232]
.global bg_mask_selector
bg_mask_selector:
    movw r1, #0
    movt r1, #0
    movs r2, #18
.global bg_mask_imp
bg_mask_imp:
    movw r12, #0
    movt r12, #0
    blx r12
    ldr.w r12, [sp, #24]
    vmsr fpscr, r12
    add sp, #32
    vpop {d16-d31}
    vpop {d0-d7}
bg_restore:
    ldr.w r12, [sp]
    add sp, #8
    msr apsr_nzcvqg, r12
    pop.w {r0-r12, lr}
.global bg_original
bg_original:
    .space 8
    .p2align 2
    .word 0xf000f8df
.global bg_return
bg_return:
    .word 0
