.syntax unified
.thumb
.fpu neon
.text
.p2align 2
.global setup_finish_template
setup_finish_template:
    push.w {r0-r12, lr}
    mrs r12, apsr
    sub sp, #8
    str.w r12, [sp]
    vpush {d0-d7}
    vpush {d16-d31}
    sub sp, #16
    vmrs r12, fpscr
    str.w r12, [sp, #8]
.global setup_perform_ref
setup_perform_ref:
    movw r1, #0
    movt r1, #0
    ldr r1, [r1]
.global setup_finish_ref
setup_finish_ref:
    movw r2, #0
    movt r2, #0
    ldr r2, [r2]
    cbz r1, setup_restore_vfp
    cbz r2, setup_restore_vfp
    movs r3, #0
    str r3, [sp]
.global setup_delay_high
setup_delay_high:
    movw r12, #0
    movt r12, #0
    str.w r12, [sp, #4]
.global setup_msgsend
setup_msgsend:
    movw r12, #0
    movt r12, #0
    blx r12
setup_restore_vfp:
    ldr.w r12, [sp, #8]
    vmsr fpscr, r12
    add sp, #16
    vpop {d16-d31}
    vpop {d0-d7}
    ldr.w r12, [sp]
    add sp, #8
    msr apsr_nzcvqg, r12
    pop.w {r0-r12, lr}
    .p2align 2
.global setup_original
setup_original:
    .space 8
    .word 0xf000f8df
.global setup_return
setup_return:
    .word 0
