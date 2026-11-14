.syntax unified
.thumb
.text
.balign 4
.global entry
.thumb_func
entry:
    cbz r0, no_job
    /* The original first eight bytes of job_mig_get_listener_port_rights. */
    push {r4, r5, r6, r7, lr}
    add r7, sp, #12
    push.w {r8, r10, r11}
    ldr.w pc, resume
no_job:
    movw r0, #1105 /* BOOTSTRAP_NO_MEMORY, as in launchd 842.92.1. */
    bx lr
.balign 4
resume:
    .word 0 /* Patched to the original entry + 8, with the Thumb bit. */
