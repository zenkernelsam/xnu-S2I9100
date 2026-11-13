.syntax unified
.thumb
.text
.global i9100_audio_property_trampoline
.thumb_func
i9100_audio_property_trampoline:
    push {r0-r3,r6,lr}
    ldr r0,[r5,#12]
    ldr r12,.Lunlock
    blx r12
    pop {r0-r3,r6,lr}
    ldr r0,[r5,#208]
    add r4,sp,#24
    movs r3,#16
    add r1,sp,#12
    stm sp,{r3,r4}
    movs r2,#0
    movs r3,#0
    blx r6
    push {r0-r3,r6,lr}
    ldr r0,[r5,#12]
    ldr r12,.Llock
    blx r12
    pop {r0-r3,r6,lr}
    ldr r12,.Lreturn
    bx r12
    .balign 4
.Lunlock: .word 0x39217ec1
.Llock: .word 0x39215fa1
.Lreturn: .word 0x3169e85d
