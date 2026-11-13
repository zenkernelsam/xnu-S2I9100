.syntax unified
.thumb
.text
.global i9100_audio_property_trampoline
.thumb_func
i9100_audio_property_trampoline:
    push {r8,r9}
    push {r0-r3,r6,lr}
    ldr r12,.Ldiag
    ldr r0,[r12]
    adds r0,r0,#1
    str r0,[r12]
    movs r0,#1
    str r0,[r12,#4]
    movs r8,#1
    ldr r0,[r5,#12]
    str r0,[r12,#8]
    str r6,[r12,#36]
    str r5,[r12,#40]
    ldr r1,[r0]
    str r1,[r12,#12]
    movw r2,#0x5458
    movt r2,#0x4d55
    cmp r1,r2
    bne .Ldepth
    ldr r1,[r0,#8]
    str r1,[r12,#16]
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
    ldr r12,.Ldiag
    str r8,[r12,#20]
    mov r9,r8
    movs r8,#0
.Lrelease:
    ldr r0,[r5,#12]
    ldr r12,.Lunlock
    blx r12
    ldr r12,.Ldiag
    str r0,[r12,#24]
    cmp r0,#0
    bne .Lquery
    add r8,r8,#1
    subs r9,r9,#1
    bne .Lrelease
.Lquery:
    ldr r12,.Ldiag
    str r8,[r12,#28]
    movs r0,#2
    str r0,[r12,#4]
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
    ldr r12,.Ldiag
    str r0,[r12,#32]
    movs r0,#3
    str r0,[r12,#4]
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
    ldr r12,.Ldiag
    movs r0,#4
    str r0,[r12,#4]
    pop {r0-r3,r6,lr}
    add sp,#8
    pop {r8,r9}
    ldr r12,.Lreturn
    bx r12
    .balign 4
.Lunlock: .word 0x39217ec1
.Llock: .word 0x39215fa1
.Lreturn: .word 0x3169e85d
.Ldiag: .word 0x00000000
