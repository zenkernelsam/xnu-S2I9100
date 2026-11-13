.syntax unified
.thumb
.text
.global i9100_audio_property_trampoline
.thumb_func
i9100_audio_property_trampoline:
    push {r8,r9}
    push {r0-r3,r6,lr}
    movs r8,#1
    ldr r0,[r5,#12]
    ldr r1,[r0]
    movw r2,#0x5458
    movt r2,#0x4d55
    cmp r1,r2
    bne .Ldepth
    ldr r1,[r0,#8]
    and r2,r1,#12
    cmp r2,#8
    bne .Ldepth
    lsrs r1,r1,#16
    cmp r1,#1
    blo .Ldepth
    cmp r1,#16
    bhi .Ldepth
    mov r8,r1
.Ldepth:
    mov r9,r8
    movs r8,#0
.Lrelease:
    ldr r0,[r5,#12]
    ldr r12,.Lunlock
    blx r12
    cmp r0,#0
    bne .Lquery
    add r8,r8,#1
    subs r9,r9,#1
    bne .Lrelease
.Lquery:
    pop {r0-r3,r6,lr}
    sub sp,#8
    ldr r0,[r5,#208]
    add r4,sp,#40
    movs r3,#16
    add r1,sp,#28
    stm sp,{r3,r4}
    movs r2,#0
    movs r3,#0
    blx r6
    push {r0-r3,r6,lr}
    cmp r8,#0
    beq .Lrestored
.Lreacquire:
    ldr r0,[r5,#12]
    ldr r12,.Llock
    blx r12
    cmp r0,#0
    bne .Lrestored
    subs r8,r8,#1
    bne .Lreacquire
.Lrestored:
    pop {r0-r3,r6,lr}
    add sp,#8
    pop {r8,r9}
    ldr r12,.Lreturn
    bx r12
    .balign 4
.Lunlock: .word 0x39217ec1
.Llock: .word 0x39215fa1
.Lreturn: .word 0x3169e85d
