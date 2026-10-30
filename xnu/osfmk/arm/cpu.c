/*
 * Copyright 2013, winocm. <winocm@icloud.com>
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 * 
 *   Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer.
 * 
 *   Redistributions in binary form must reproduce the above copyright notice, this
 *   list of conditions and the following disclaimer in the documentation and/or
 *   other materials provided with the distribution.
 * 
 *   If you are going to use this software in any form that does not involve
 *   releasing the source to this project or improving it, let me know beforehand.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/*-
 * Copyright (c) 1995 Mark Brinicombe.
 * Copyright (c) 1995 Brini.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *  This product includes software developed by Brini.
 * 4. The name of the company nor the name of the author may be used to
 *    endorse or promote products derived from this software without specific
 *    prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY BRINI ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL BRINI OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * RiscBSD kernel project
 *
 * cpu.c
 *
 * Probing and configuration for the master CPU
 *
 * Created      : 10/10/95
 */
/*
 * CPU bootstrap, used to create core structures for the boot processor.
 *
 * SMP support coming soon.
 */

#include <kern/kalloc.h>
#include <platforms.h>
#include <kern/misc_protos.h>
#include <kern/machine.h>
#include <mach/processor_info.h>
#include <arm/pmap.h>
#include <arm/machine_routines.h>
#include <vm/vm_kern.h>
#include <kern/timer_call.h>
#include <kern/etimer.h>
#include <kern/processor.h>
#include <arm/misc_protos.h>
#include <mach/machine.h>
#include <arm/arch.h>
#include "proc_reg.h"
#if defined(BOARD_CONFIG_EXYNOS4210_I9100)
#include <kern/startup.h>
#include <kern/sched_prim.h>
#include <arm/cpu_data.h>
#include <arm/cpufunc.h>
#endif

/*
 * Initial stack for first CPU.
 */
extern uint32_t intstack_top[]; /* top */
extern uint32_t intstack[];     /* bottom */

struct processor BootProcessor;
cpu_data_t cpu_data_master;

enum cpu_class {
    CPU_CLASS_NONE,
    CPU_CLASS_ARM2,
    CPU_CLASS_ARM2AS,
    CPU_CLASS_ARM3,
    CPU_CLASS_ARM6,
    CPU_CLASS_ARM7,
    CPU_CLASS_ARM7TDMI,
    CPU_CLASS_ARM8,
    CPU_CLASS_ARM9TDMI,
    CPU_CLASS_ARM9ES,
    CPU_CLASS_ARM9EJS,
    CPU_CLASS_ARM10E,
    CPU_CLASS_ARM10EJ,
    CPU_CLASS_CORTEXA,
    CPU_CLASS_SA1,
    CPU_CLASS_XSCALE,
    CPU_CLASS_ARM11J,
    CPU_CLASS_MARVELL,
    CPU_CLASS_SCORPION
};

static const char *const generic_steppings[16] = {
    "rev 0", "rev 1", "rev 2", "rev 3",
    "rev 4", "rev 5", "rev 6", "rev 7",
    "rev 8", "rev 9", "rev 10", "rev 11",
    "rev 12", "rev 13", "rev 14", "rev 15",
};

static const char *const sa110_steppings[16] = {
    "rev 0", "step J", "step K", "step S",
    "step T", "rev 5", "rev 6", "rev 7",
    "rev 8", "rev 9", "rev 10", "rev 11",
    "rev 12", "rev 13", "rev 14", "rev 15",
};

static const char *const sa1100_steppings[16] = {
    "rev 0", "step B", "step C", "rev 3",
    "rev 4", "rev 5", "rev 6", "rev 7",
    "step D", "step E", "rev 10" "step G",
    "rev 12", "rev 13", "rev 14", "rev 15",
};

static const char *const sa1110_steppings[16] = {
    "step A-0", "rev 1", "rev 2", "rev 3",
    "step B-0", "step B-1", "step B-2", "step B-3",
    "step B-4", "step B-5", "rev 10", "rev 11",
    "rev 12", "rev 13", "rev 14", "rev 15",
};

static const char *const ixp12x0_steppings[16] = {
    "(IXP1200 step A)", "(IXP1200 step B)",
    "rev 2", "(IXP1200 step C)",
    "(IXP1200 step D)", "(IXP1240/1250 step A)",
    "(IXP1240 step B)", "(IXP1250 step B)",
    "rev 8", "rev 9", "rev 10", "rev 11",
    "rev 12", "rev 13", "rev 14", "rev 15",
};

static const char *const xscale_steppings[16] = {
    "step A-0", "step A-1", "step B-0", "step C-0",
    "step D-0", "rev 5", "rev 6", "rev 7",
    "rev 8", "rev 9", "rev 10", "rev 11",
    "rev 12", "rev 13", "rev 14", "rev 15",
};

static const char *const i80219_steppings[16] = {
    "step A-0", "rev 1", "rev 2", "rev 3",
    "rev 4", "rev 5", "rev 6", "rev 7",
    "rev 8", "rev 9", "rev 10", "rev 11",
    "rev 12", "rev 13", "rev 14", "rev 15",
};

static const char *const i80321_steppings[16] = {
    "step A-0", "step B-0", "rev 2", "rev 3",
    "rev 4", "rev 5", "rev 6", "rev 7",
    "rev 8", "rev 9", "rev 10", "rev 11",
    "rev 12", "rev 13", "rev 14", "rev 15",
};

static const char *const i81342_steppings[16] = {
    "step A-0", "rev 1", "rev 2", "rev 3",
    "rev 4", "rev 5", "rev 6", "rev 7",
    "rev 8", "rev 9", "rev 10", "rev 11",
    "rev 12", "rev 13", "rev 14", "rev 15",
};

/* Steppings for PXA2[15]0 */
static const char *const pxa2x0_steppings[16] = {
    "step A-0", "step A-1", "step B-0", "step B-1",
    "step B-2", "step C-0", "rev 6", "rev 7",
    "rev 8", "rev 9", "rev 10", "rev 11",
    "rev 12", "rev 13", "rev 14", "rev 15",
};

/* Steppings for PXA255/26x.
 * rev 5: PXA26x B0, rev 6: PXA255 A0
 */
static const char *const pxa255_steppings[16] = {
    "rev 0", "rev 1", "rev 2", "step A-0",
    "rev 4", "step B-0", "step A-0", "rev 7",
    "rev 8", "rev 9", "rev 10", "rev 11",
    "rev 12", "rev 13", "rev 14", "rev 15",
};

/* Stepping for PXA27x */
static const char *const pxa27x_steppings[16] = {
    "step A-0", "step A-1", "step B-0", "step B-1",
    "step C-0", "rev 5", "rev 6", "rev 7",
    "rev 8", "rev 9", "rev 10", "rev 11",
    "rev 12", "rev 13", "rev 14", "rev 15",
};

static const char *const ixp425_steppings[16] = {
    "step 0 (A0)", "rev 1 (ARMv5TE)", "rev 2", "rev 3",
    "rev 4", "rev 5", "rev 6", "rev 7",
    "rev 8", "rev 9", "rev 10", "rev 11",
    "rev 12", "rev 13", "rev 14", "rev 15",
};

struct cpuidtab {
    u_int32_t cpuid;
    enum cpu_class cpu_class;
    const char *cpu_name;
    const char *const *cpu_steppings;
};

const struct cpuidtab cpuids[] = {
    {CPU_ID_ARM2, CPU_CLASS_ARM2, "ARM2",
     generic_steppings},
    {CPU_ID_ARM250, CPU_CLASS_ARM2AS, "ARM250",
     generic_steppings},

    {CPU_ID_ARM3, CPU_CLASS_ARM3, "ARM3",
     generic_steppings},

    {CPU_ID_ARM600, CPU_CLASS_ARM6, "ARM600",
     generic_steppings},
    {CPU_ID_ARM610, CPU_CLASS_ARM6, "ARM610",
     generic_steppings},
    {CPU_ID_ARM620, CPU_CLASS_ARM6, "ARM620",
     generic_steppings},

    {CPU_ID_ARM700, CPU_CLASS_ARM7, "ARM700",
     generic_steppings},
    {CPU_ID_ARM710, CPU_CLASS_ARM7, "ARM710",
     generic_steppings},
    {CPU_ID_ARM7500, CPU_CLASS_ARM7, "ARM7500",
     generic_steppings},
    {CPU_ID_ARM710A, CPU_CLASS_ARM7, "ARM710a",
     generic_steppings},
    {CPU_ID_ARM7500FE, CPU_CLASS_ARM7, "ARM7500FE",
     generic_steppings},
    {CPU_ID_ARM710T, CPU_CLASS_ARM7TDMI, "ARM710T",
     generic_steppings},
    {CPU_ID_ARM720T, CPU_CLASS_ARM7TDMI, "ARM720T",
     generic_steppings},
    {CPU_ID_ARM740T8K, CPU_CLASS_ARM7TDMI, "ARM740T (8 KB cache)",
     generic_steppings},
    {CPU_ID_ARM740T4K, CPU_CLASS_ARM7TDMI, "ARM740T (4 KB cache)",
     generic_steppings},

    {CPU_ID_ARM810, CPU_CLASS_ARM8, "ARM810",
     generic_steppings},

    {CPU_ID_ARM920T, CPU_CLASS_ARM9TDMI, "ARM920T",
     generic_steppings},
    {CPU_ID_ARM920T_ALT, CPU_CLASS_ARM9TDMI, "ARM920T",
     generic_steppings},
    {CPU_ID_ARM922T, CPU_CLASS_ARM9TDMI, "ARM922T",
     generic_steppings},
    {CPU_ID_ARM926EJS, CPU_CLASS_ARM9EJS, "ARM926EJ-S",
     generic_steppings},
    {CPU_ID_ARM940T, CPU_CLASS_ARM9TDMI, "ARM940T",
     generic_steppings},
    {CPU_ID_ARM946ES, CPU_CLASS_ARM9ES, "ARM946E-S",
     generic_steppings},
    {CPU_ID_ARM966ES, CPU_CLASS_ARM9ES, "ARM966E-S",
     generic_steppings},
    {CPU_ID_ARM966ESR1, CPU_CLASS_ARM9ES, "ARM966E-S",
     generic_steppings},
    {CPU_ID_FA526, CPU_CLASS_ARM9TDMI, "FA526",
     generic_steppings},
    {CPU_ID_FA626TE, CPU_CLASS_ARM9ES, "FA626TE",
     generic_steppings},

    {CPU_ID_TI925T, CPU_CLASS_ARM9TDMI, "TI ARM925T",
     generic_steppings},

    {CPU_ID_ARM1020E, CPU_CLASS_ARM10E, "ARM1020E",
     generic_steppings},
    {CPU_ID_ARM1022ES, CPU_CLASS_ARM10E, "ARM1022E-S",
     generic_steppings},
    {CPU_ID_ARM1026EJS, CPU_CLASS_ARM10EJ, "ARM1026EJ-S",
     generic_steppings},

    {CPU_ID_CORTEXA5, CPU_CLASS_CORTEXA, "Cortex A5",
     generic_steppings},
    {CPU_ID_CORTEXA7, CPU_CLASS_CORTEXA, "Cortex A7",
     generic_steppings},
    {CPU_ID_CORTEXA8R0, CPU_CLASS_CORTEXA, "Cortex A8-r0",
     generic_steppings},
    {CPU_ID_CORTEXA8R1, CPU_CLASS_CORTEXA, "Cortex A8-r1",
     generic_steppings},
    {CPU_ID_CORTEXA8R2, CPU_CLASS_CORTEXA, "Cortex A8-r2",
     generic_steppings},
    {CPU_ID_CORTEXA8R3, CPU_CLASS_CORTEXA, "Cortex A8-r3",
     generic_steppings},
    {CPU_ID_CORTEXA9R1, CPU_CLASS_CORTEXA, "Cortex A9-r1",
     generic_steppings},
    {CPU_ID_CORTEXA9R2, CPU_CLASS_CORTEXA, "Cortex A9-r2",
     generic_steppings},
    {CPU_ID_CORTEXA9R3, CPU_CLASS_CORTEXA, "Cortex A9-r3",
     generic_steppings},
    {CPU_ID_CORTEXA15, CPU_CLASS_CORTEXA, "Cortex A15",
     generic_steppings},
    {CPU_ID_SCORPION, CPU_CLASS_SCORPION, "Qualcomm Scorpion",
     generic_steppings},

    {CPU_ID_SA110, CPU_CLASS_SA1, "SA-110",
     sa110_steppings},
    {CPU_ID_SA1100, CPU_CLASS_SA1, "SA-1100",
     sa1100_steppings},
    {CPU_ID_SA1110, CPU_CLASS_SA1, "SA-1110",
     sa1110_steppings},

    {CPU_ID_IXP1200, CPU_CLASS_SA1, "IXP1200",
     ixp12x0_steppings},

    {CPU_ID_80200, CPU_CLASS_XSCALE, "i80200",
     xscale_steppings},

    {CPU_ID_80321_400, CPU_CLASS_XSCALE, "i80321 400MHz",
     i80321_steppings},
    {CPU_ID_80321_600, CPU_CLASS_XSCALE, "i80321 600MHz",
     i80321_steppings},
    {CPU_ID_80321_400_B0, CPU_CLASS_XSCALE, "i80321 400MHz",
     i80321_steppings},
    {CPU_ID_80321_600_B0, CPU_CLASS_XSCALE, "i80321 600MHz",
     i80321_steppings},

    {CPU_ID_81342, CPU_CLASS_XSCALE, "i81342",
     i81342_steppings},

    {CPU_ID_80219_400, CPU_CLASS_XSCALE, "i80219 400MHz",
     i80219_steppings},
    {CPU_ID_80219_600, CPU_CLASS_XSCALE, "i80219 600MHz",
     i80219_steppings},

    {CPU_ID_PXA27X, CPU_CLASS_XSCALE, "PXA27x",
     pxa27x_steppings},
    {CPU_ID_PXA250A, CPU_CLASS_XSCALE, "PXA250",
     pxa2x0_steppings},
    {CPU_ID_PXA210A, CPU_CLASS_XSCALE, "PXA210",
     pxa2x0_steppings},
    {CPU_ID_PXA250B, CPU_CLASS_XSCALE, "PXA250",
     pxa2x0_steppings},
    {CPU_ID_PXA210B, CPU_CLASS_XSCALE, "PXA210",
     pxa2x0_steppings},
    {CPU_ID_PXA250C, CPU_CLASS_XSCALE, "PXA255",
     pxa255_steppings},
    {CPU_ID_PXA210C, CPU_CLASS_XSCALE, "PXA210",
     pxa2x0_steppings},

    {CPU_ID_IXP425_533, CPU_CLASS_XSCALE, "IXP425 533MHz",
     ixp425_steppings},
    {CPU_ID_IXP425_400, CPU_CLASS_XSCALE, "IXP425 400MHz",
     ixp425_steppings},
    {CPU_ID_IXP425_266, CPU_CLASS_XSCALE, "IXP425 266MHz",
     ixp425_steppings},

    /*
     * XXX ixp435 steppings? 
     */
    {CPU_ID_IXP435, CPU_CLASS_XSCALE, "IXP435",
     ixp425_steppings},

    {CPU_ID_ARM1136JS, CPU_CLASS_ARM11J, "ARM1136J-S",
     generic_steppings},
    {CPU_ID_ARM1136JSR1, CPU_CLASS_ARM11J, "ARM1136J-S R1",
     generic_steppings},
    {CPU_ID_ARM1176JZS, CPU_CLASS_ARM11J, "ARM1176JZ-S",
     generic_steppings},

    {CPU_ID_MV88FR131, CPU_CLASS_MARVELL, "Feroceon 88FR131",
     generic_steppings},

    {CPU_ID_MV88FR571_VD, CPU_CLASS_MARVELL, "Feroceon 88FR571-VD",
     generic_steppings},
    {CPU_ID_MV88SV581X_V7, CPU_CLASS_MARVELL, "Sheeva 88SV581x",
     generic_steppings},
    {CPU_ID_ARM_88SV581X_V7, CPU_CLASS_MARVELL, "Sheeva 88SV581x",
     generic_steppings},
    {CPU_ID_MV88SV584X_V7, CPU_CLASS_MARVELL, "Sheeva 88SV584x",
     generic_steppings},

    {0, CPU_CLASS_NONE, NULL, NULL}
};

struct cpu_classtab {
    const char *class_name;
    const char *class_option;
};

const struct cpu_classtab cpu_classes[] = {
    {"unknown", NULL},          /* CPU_CLASS_NONE */
    {"ARM2", "CPU_ARM2"},       /* CPU_CLASS_ARM2 */
    {"ARM2as", "CPU_ARM250"},   /* CPU_CLASS_ARM2AS */
    {"ARM3", "CPU_ARM3"},       /* CPU_CLASS_ARM3 */
    {"ARM6", "CPU_ARM6"},       /* CPU_CLASS_ARM6 */
    {"ARM7", "CPU_ARM7"},       /* CPU_CLASS_ARM7 */
    {"ARM7TDMI", "CPU_ARM7TDMI"},   /* CPU_CLASS_ARM7TDMI */
    {"ARM8", "CPU_ARM8"},       /* CPU_CLASS_ARM8 */
    {"ARM9TDMI", "CPU_ARM9TDMI"},   /* CPU_CLASS_ARM9TDMI */
    {"ARM9E-S", "CPU_ARM9E"},   /* CPU_CLASS_ARM9ES */
    {"ARM9EJ-S", "CPU_ARM9E"},  /* CPU_CLASS_ARM9EJS */
    {"ARM10E", "CPU_ARM10"},    /* CPU_CLASS_ARM10E */
    {"ARM10EJ", "CPU_ARM10"},   /* CPU_CLASS_ARM10EJ */
    {"Cortex-A", "CPU_CORTEXA"},    /* CPU_CLASS_CORTEXA */
    {"SA-1", "CPU_SA110"},      /* CPU_CLASS_SA1 */
    {"XScale", "CPU_XSCALE_..."},   /* CPU_CLASS_XSCALE */
    {"ARM11J", "CPU_ARM11"},    /* CPU_CLASS_ARM11J */
    {"Marvell", "CPU_MARVELL"}, /* CPU_CLASS_MARVELL */
    {"Scorpion ARMv7", "CPU_SCORPION"}, /* CPU_CLASS_SCORPION */
};

/*
 * Report the type of the specified arm processor. This uses the generic and
 * arm specific information in the cpu structure to identify the processor.
 * The remaining fields in the cpu structure are filled in appropriately.
 */

static const char *const wtnames[] = {
    "write-through",
    "write-back",
    "write-back",
    "**unknown 3**",
    "**unknown 4**",
    "write-back-locking",       /* XXX XScale-specific? */
    "write-back-locking-A",
    "write-back-locking-B",
    "**unknown 8**",
    "**unknown 9**",
    "**unknown 10**",
    "**unknown 11**",
    "**unknown 12**",
    "**unknown 13**",
    "write-back-locking-C",
    "**unknown 15**",
};

int arm_picache_size;
int arm_picache_line_size;
int arm_picache_ways;

int arm_pdcache_size;           /* and unified */
int arm_pdcache_line_size;
int arm_pdcache_ways;

int arm_pcache_type;
int arm_pcache_unified;

int arm_dcache_align;
int arm_dcache_align_mask;

u_int arm_cache_level;
u_int arm_cache_type[14];
u_int arm_cache_loc;

static int arm_dcache_l2_nsets;
static int arm_dcache_l2_assoc;
static int arm_dcache_l2_linesize;

arm_processor_id_t arm_processor_id;

void get_cachetype_cp15()
{
    u_int ctype, isize, dsize, cpuid;
    u_int clevel, csize, i, sel;
    u_int multiplier;
    u_char type;

    __asm __volatile("mrc p15, 0, %0, c0, c0, 1":"=r"(ctype));

    cpuid = armreg_midr_read();
    arm_processor_id.processor_midr = cpuid;

    /*
     * ...and thus spake the ARM ARM:
     *
     * If an <opcode2> value corresponding to an unimplemented or
     * reserved ID register is encountered, the System Control
     * processor returns the value of the main ID register.
     */
    if (ctype == cpuid)
        goto out;

    if (CPU_CT_FORMAT(ctype) == CPU_CT_ARMV7) {
        __asm __volatile("mrc p15, 1, %0, c0, c0, 1":"=r"(clevel));
        arm_cache_level = clevel;
        arm_cache_loc = CPU_CLIDR_LOC(arm_cache_level);
        i = 0;
        while ((type = (clevel & 0x7)) && i < 7) {
            if (type == CACHE_DCACHE || type == CACHE_UNI_CACHE ||
                type == CACHE_SEP_CACHE) {
                sel = i << 1;
                __asm __volatile("mcr p15, 2, %0, c0, c0, 0"::"r"(sel));
#if defined(BOARD_CONFIG_QSD8250_LEO) || defined(BOARD_CONFIG_EXYNOS3475_J1XLTE) || defined(BOARD_CONFIG_EXYNOS4210_I9100)
                __asm __volatile("isb sy" ::: "memory");
#endif
                __asm __volatile("mrc p15, 1, %0, c0, c0, 0":"=r"(csize));
                arm_cache_type[sel] = csize;
                arm_dcache_align = 1 << (CPUV7_CT_xSIZE_LEN(csize) + 4);
                arm_dcache_align_mask = arm_dcache_align - 1;
            }
            if (type == CACHE_ICACHE || type == CACHE_SEP_CACHE) {
                sel = (i << 1) | 1;
                __asm __volatile("mcr p15, 2, %0, c0, c0, 0"::"r"(sel));
#if defined(BOARD_CONFIG_QSD8250_LEO) || defined(BOARD_CONFIG_EXYNOS3475_J1XLTE) || defined(BOARD_CONFIG_EXYNOS4210_I9100)
                __asm __volatile("isb sy" ::: "memory");
#endif
                __asm __volatile("mrc p15, 1, %0, c0, c0, 0":"=r"(csize));
                arm_cache_type[sel] = csize;
            }
            i++;
            clevel >>= 3;
        }
#if defined(BOARD_CONFIG_QSD8250_LEO) || defined(BOARD_CONFIG_EXYNOS3475_J1XLTE) || defined(BOARD_CONFIG_EXYNOS4210_I9100)
        sel = 0;
        __asm __volatile("mcr p15, 2, %0, c0, c0, 0"::"r"(sel));
        __asm __volatile("isb sy" ::: "memory");
#endif
    } else {
        if ((ctype & CPU_CT_S) == 0)
            arm_pcache_unified = 1;

        /*
         * If you want to know how this code works, go read the ARM ARM.
         */

        arm_pcache_type = CPU_CT_CTYPE(ctype);

        if (arm_pcache_unified == 0) {
            isize = CPU_CT_ISIZE(ctype);
            multiplier = (isize & CPU_CT_xSIZE_M) ? 3 : 2;
            arm_picache_line_size = 1U << (CPU_CT_xSIZE_LEN(isize) + 3);
            if (CPU_CT_xSIZE_ASSOC(isize) == 0) {
                if (isize & CPU_CT_xSIZE_M)
                    arm_picache_line_size = 0;  /* not present */
                else
                    arm_picache_ways = 1;
            } else {
                arm_picache_ways = multiplier << (CPU_CT_xSIZE_ASSOC(isize) - 1);
            }
            arm_picache_size = multiplier << (CPU_CT_xSIZE_SIZE(isize) + 8);
        }

        dsize = CPU_CT_DSIZE(ctype);
        multiplier = (dsize & CPU_CT_xSIZE_M) ? 3 : 2;
        arm_pdcache_line_size = 1U << (CPU_CT_xSIZE_LEN(dsize) + 3);
        if (CPU_CT_xSIZE_ASSOC(dsize) == 0) {
            if (dsize & CPU_CT_xSIZE_M)
                arm_pdcache_line_size = 0;  /* not present */
            else
                arm_pdcache_ways = 1;
        } else {
            arm_pdcache_ways = multiplier << (CPU_CT_xSIZE_ASSOC(dsize) - 1);
        }
        arm_pdcache_size = multiplier << (CPU_CT_xSIZE_SIZE(dsize) + 8);

        arm_dcache_align = arm_pdcache_line_size;

        arm_dcache_l2_assoc = CPU_CT_xSIZE_ASSOC(dsize) + multiplier - 2;
        arm_dcache_l2_linesize = CPU_CT_xSIZE_LEN(dsize) + 3;
        arm_dcache_l2_nsets = 6 + CPU_CT_xSIZE_SIZE(dsize) -
            CPU_CT_xSIZE_ASSOC(dsize) - CPU_CT_xSIZE_LEN(dsize);

 out:
        arm_dcache_align_mask = arm_dcache_align - 1;
    }
}

static void print_enadis(int enadis, char *s)
{

    kprintf(" %s %sabled", s, (enadis == 0) ? "dis" : "en");
}

int ctrl;
enum cpu_class cpu_class = CPU_CLASS_NONE;

u_int cpu_pfr(int num)
{
    u_int feat = 0;

    switch (num) {
    case 0:
 __asm __volatile("mrc p15, 0, %0, c0, c1, 0":"=r"(feat));
        break;
    case 1:
 __asm __volatile("mrc p15, 0, %0, c0, c1, 1":"=r"(feat));
        break;
    default:
        panic("Processor Feature Register %d not implemented", num);
        break;
    }

    return (feat);
}

static
void identify_armv7(void)
{
    u_int feature;

    kprintf("Supported features:");
    /*
     * Get Processor Feature Register 0 
     */
    feature = cpu_pfr(0);

    if (feature & ARM_PFR0_ARM_ISA_MASK) {
        kprintf(" ARM_ISA");
        arm_processor_id.processor_features |= kProcessorFeatureARM_ISA;
    }

    if (feature & ARM_PFR0_THUMB2) {
        kprintf(" THUMB2"); 
        arm_processor_id.processor_features |= kProcessorFeatureThumb2 | kProcessorFeatureThumb;
    } else if (feature & ARM_PFR0_THUMB) {
        kprintf(" THUMB");
        arm_processor_id.processor_features |= kProcessorFeatureThumb;
    }

    if (feature & ARM_PFR0_JAZELLE_MASK) {
        kprintf(" JAZELLE");
        arm_processor_id.processor_features |= kProcessorFeatureJazelle;
    }

    if (feature & ARM_PFR0_THUMBEE_MASK) {
        kprintf(" THUMBEE");
        arm_processor_id.processor_features |= kProcessorFeatureThumbEE;
    }

    /*
     * Get Processor Feature Register 1 
     */
    feature = cpu_pfr(1);

    if (feature & ARM_PFR1_ARMV4_MASK) {
        kprintf(" ARMv4");
        arm_processor_id.processor_features |= kProcessorFeatureARMv4;
    }

    if (feature & ARM_PFR1_SEC_EXT_MASK) {
        kprintf(" Security_Ext");
        arm_processor_id.processor_features |= kProcessorFeatureSecurity;
    }

    if (feature & ARM_PFR1_MICROCTRL_MASK) {
        kprintf(" M_profile");
        arm_processor_id.processor_features |= kProcessorFeatureMicrocontroller;
    }

    kprintf("\n");
}

void identify_arm_cpu(void)
{
    u_int cpuid, reg, size, sets, ways;
    u_int8_t type, linesize;
    int i;

    cpuid = armreg_midr_read();
    ctrl = armreg_sctrl_read();

    if (cpuid == 0) {
        kprintf("Processor failed probe - no CPU ID\n");
        return;
    }

    for (i = 0; cpuids[i].cpuid != 0; i++)
        if (cpuids[i].cpuid == (cpuid & CPU_ID_CPU_MASK)) {
            cpu_class = cpuids[i].cpu_class;
            arm_processor_id.processor_class = cpuids[i].cpu_name;
            kprintf("CPU: %s %s (%s core)\n",
                   cpuids[i].cpu_name,
                   cpuids[i].cpu_steppings[cpuid &
                                           CPU_ID_REVISION_MASK],
                   cpu_classes[cpu_class].class_name);
            break;
        }
    if (cpuids[i].cpuid == 0) {
        kprintf("unknown CPU (ID = 0x%x)\n", cpuid);
        arm_processor_id.processor_class = "Unknown";
    }

    kprintf(" ");

    if ((cpuid & CPU_ID_ARCH_MASK) == CPU_ID_CPUID_SCHEME) {
        identify_armv7();
    } else {
        if (ctrl & CPU_CONTROL_BEND_ENABLE)
            kprintf(" Big-endian");
        else
            kprintf(" Little-endian");

        switch (cpu_class) {
        case CPU_CLASS_ARM6:
        case CPU_CLASS_ARM7:
        case CPU_CLASS_ARM7TDMI:
        case CPU_CLASS_ARM8:
            print_enadis(ctrl & CPU_CONTROL_IDC_ENABLE, "IDC");
            break;
        case CPU_CLASS_ARM9TDMI:
        case CPU_CLASS_ARM9ES:
        case CPU_CLASS_ARM9EJS:
        case CPU_CLASS_ARM10E:
        case CPU_CLASS_ARM10EJ:
        case CPU_CLASS_SA1:
        case CPU_CLASS_XSCALE:
        case CPU_CLASS_ARM11J:
        case CPU_CLASS_MARVELL:
            print_enadis(ctrl & CPU_CONTROL_DC_ENABLE, "DC");
            print_enadis(ctrl & CPU_CONTROL_IC_ENABLE, "IC");
#ifdef CPU_XSCALE_81342
            print_enadis(ctrl & CPU_CONTROL_L2_ENABLE, "L2");
#endif
#if defined(SOC_MV_KIRKWOOD) || defined(SOC_MV_DISCOVERY)
            i = sheeva_control_ext(0, 0);
            print_enadis(i & MV_WA_ENABLE, "WA");
            print_enadis(i & MV_DC_STREAM_ENABLE, "DC streaming");
            kprintf("\n ");
            print_enadis((i & MV_BTB_DISABLE) == 0, "BTB");
            print_enadis(i & MV_L2_ENABLE, "L2");
            print_enadis((i & MV_L2_PREFETCH_DISABLE) == 0, "L2 prefetch");
            kprintf("\n ");
#endif
            break;
        default:
            break;
        }
    }

    print_enadis(ctrl & CPU_CONTROL_WBUF_ENABLE, "WB");
    if (ctrl & CPU_CONTROL_LABT_ENABLE)
        kprintf(" LABT");
    else
        kprintf(" EABT");

    print_enadis(ctrl & CPU_CONTROL_BPRD_ENABLE, "branch prediction");
    kprintf("\n");

    if (arm_cache_level) {
        kprintf("LoUU:%d LoC:%d LoUIS:%d \n", CPU_CLIDR_LOUU(arm_cache_level) + 1,
               arm_cache_loc, CPU_CLIDR_LOUIS(arm_cache_level) + 1);
        i = 0;
        while (((type = CPU_CLIDR_CTYPE(arm_cache_level, i)) != 0) && i < 7) {
            kprintf("Cache level %d: \n", i + 1);
            if (type == CACHE_DCACHE || type == CACHE_UNI_CACHE ||
                type == CACHE_SEP_CACHE) {
                reg = arm_cache_type[2 * i];
                ways = CPUV7_CT_xSIZE_ASSOC(reg) + 1;
                sets = CPUV7_CT_xSIZE_SET(reg) + 1;
                linesize = 1 << (CPUV7_CT_xSIZE_LEN(reg) + 4);
                size = (ways * sets * linesize) / 1024;

                arm_processor_id.cache_levels[i].linesize = linesize;
                arm_processor_id.cache_levels[i].ways = ways;
                arm_processor_id.cache_levels[i].size = (ways * sets * linesize);

                if (type == CACHE_UNI_CACHE)
                    kprintf(" %dKB/%dB %d-way unified cache", size, linesize, ways);
                else
                    kprintf(" %dKB/%dB %d-way data cache", size, linesize, ways);
                if (reg & CPUV7_CT_CTYPE_WT)
                    kprintf(" WT");
                if (reg & CPUV7_CT_CTYPE_WB)
                    kprintf(" WB");
                if (reg & CPUV7_CT_CTYPE_RA)
                    kprintf(" Read-Alloc");
                if (reg & CPUV7_CT_CTYPE_WA)
                    kprintf(" Write-Alloc");
                kprintf("\n");
            }

            if (type == CACHE_ICACHE || type == CACHE_SEP_CACHE) {
                reg = arm_cache_type[(2 * i) + 1];

                ways = CPUV7_CT_xSIZE_ASSOC(reg) + 1;
                sets = CPUV7_CT_xSIZE_SET(reg) + 1;
                linesize = 1 << (CPUV7_CT_xSIZE_LEN(reg) + 4);
                size = (ways * sets * linesize) / 1024;

                kprintf(" %dKB/%dB %d-way instruction cache", size, linesize, ways);
                if (reg & CPUV7_CT_CTYPE_WT)
                    kprintf(" WT");
                if (reg & CPUV7_CT_CTYPE_WB)
                    kprintf(" WB");
                if (reg & CPUV7_CT_CTYPE_RA)
                    kprintf(" Read-Alloc");
                if (reg & CPUV7_CT_CTYPE_WA)
                    kprintf(" Write-Alloc");
                kprintf("\n");
            }
            i++;
        }
    } else {
        /*
         * Print cache info. 
         */
        if (arm_picache_line_size == 0 && arm_pdcache_line_size == 0)
            return;

        if (arm_pcache_unified) {
            kprintf("  %dKB/%dB %d-way %s unified cache\n",
                   arm_pdcache_size / 1024,
                   arm_pdcache_line_size, arm_pdcache_ways, wtnames[arm_pcache_type]);
        } else {
            kprintf("  %dKB/%dB %d-way instruction cache\n",
                   arm_picache_size / 1024, arm_picache_line_size, arm_picache_ways);
            kprintf("  %dKB/%dB %d-way %s data cache\n",
                   arm_pdcache_size / 1024,
                   arm_pdcache_line_size, arm_pdcache_ways, wtnames[arm_pcache_type]);
        }
    }
}

/**
 * cpu_bootstrap
 *
 * Initialize core processor data for CPU #0 during initialization.
 */
#if defined(BOARD_CONFIG_EXYNOS4210_I9100)
static cpu_data_t i9100_cpu1_data;
#endif

void cpu_bootstrap(void)
{
    cpu_data_ptr[0] = &cpu_data_master;
#if defined(BOARD_CONFIG_EXYNOS4210_I9100)
    /* cpu_data_ptr lives in a read-only region after early init, so the
     * second slot must be registered here, before page protection. The
     * struct contents are filled later by i9100_smp_init(). */
    cpu_data_ptr[1] = &i9100_cpu1_data;
#endif

    cpu_data_master.cpu_this = &cpu_data_master;
    cpu_data_master.cpu_processor = &BootProcessor;
    cpu_data_master.cpu_int_stack_top = (vm_offset_t)intstack_top;
}

/**
 * cpu_init
 *
 * Initialize more core processor data for CPU #0 during initialization.
 */

/* Stub definitions. */
extern void fleh_reset(void);
extern void fleh_swi(void);
extern void fleh_undef(void);
extern void fleh_prefabt(void);
extern void fleh_dataabt(void);
extern void fleh_dataexc(void);
extern void fleh_irq(void);

void cpu_init(void)
{
    cpu_data_t *cdp = current_cpu_datap();

    timer_call_initialize_queue(&cdp->rt_timer.queue);
    cdp->rt_timer.deadline = EndOfAllTime;

    cdp->cpu_type = CPU_TYPE_ARM;
#if defined(_ARM_ARCH_7)
    cdp->cpu_subtype = CPU_SUBTYPE_ARM_V7;
#elif defined(_ARM_ARCH_V6)
    cdp->cpu_subtype = CPU_SUBTYPE_ARM_V6;
#else
    cdp->cpu_subtype = CPU_SUBTYPE_ARM_ALL;
#endif

    cdp->fleh_reset = (uint32_t)&fleh_reset;
    cdp->fleh_swi = (uint32_t)&fleh_swi;
    cdp->fleh_undef = (uint32_t)&fleh_undef;
    cdp->fleh_prefabt = (uint32_t)&fleh_prefabt;
    cdp->fleh_dataabt = (uint32_t)&fleh_dataabt;
    cdp->fleh_dataexc = (uint32_t)&fleh_dataexc;
    cdp->fleh_irq = (uint32_t)&fleh_irq;
}

/**
 * get_cpu_number
 * 
 * Return the current processor number from the PCB. (Right now since we have
 * no SMP support, we have only 1 processor.
 *
 * When SMP becomes of real importance, we can get the current MPCore number
 * by reading some registers in the CP15 coprocessor.
 */
int get_cpu_number(void)
{
#if defined(BOARD_CONFIG_EXYNOS4210_I9100)
    return armreg_mpidr_read() & 0x3;
#else
    return 0;
#endif
}

/**
 * current_cpu_datap
 * 
 * Return the current processor PCB.
 */
cpu_data_t *current_cpu_datap(void)
{
    int smp_number = get_cpu_number();
    cpu_data_t *current_cpu_data;

    if (smp_number == 0)
        return &cpu_data_master;

    current_cpu_data = cpu_datap(smp_number);
    if (!current_cpu_data) {
        panic("cpu_data for slot %d is not available yet\n", smp_number);
    }

    return current_cpu_data;
}

/**
 * cpu_processor_alloc
 *
 * Allocate a processor_t data structure for the specified processor.
 * The boot processor will always use internal data, since vm won't be
 * initialized to allocate data.
 */
processor_t cpu_processor_alloc(boolean_t is_boot_cpu)
{
    int ret;
    processor_t proc;

    if (is_boot_cpu) {
        return &BootProcessor;
    }

    /*
     * Allocate a new processor. 
     */
    ret = kmem_alloc(kernel_map, (vm_offset_t *) & proc, sizeof(*proc));
    if (ret != KERN_SUCCESS)
        return NULL;

    bzero((void *) proc, sizeof(*proc));
    return proc;
}

/**
 * current_processor
 *
 * Return the processor_t to the for the current processor.
 */
processor_t current_processor(void)
{
    return current_cpu_datap()->cpu_processor;
}

/**
 * cpu_to_processor
 *
 * Return the procesor_t for a specified processor. Please don't
 * do bad things. 
 *
 * This function needs validation to ensure that the cpu data accessed
 * isn't null/exceeding buffer boundaries. 
 */
processor_t cpu_to_processor(int cpu)
{
    assert(cpu_datap(cpu) != NULL);
    return cpu_datap(cpu)->cpu_processor;
}

/*
 * For sysctl().
 */
cpu_type_t cpu_type(void)
{
    return current_cpu_datap()->cpu_type;
}

/**
 * slot_type
 *
 * Return the current cpu type for a specified processor.
 */
cpu_type_t slot_type(int slot_num)
{
    return (cpu_datap(slot_num)->cpu_type);
}

/**
 * slot_subtype
 *
 * Return the current cpu subtype for a specified processor.
 */
cpu_subtype_t slot_subtype(int slot_num)
{
    return (cpu_datap(slot_num)->cpu_subtype);
}

/**
 * slot_threadtype
 *
 * Return the current SMT type for a specified processor.
 */
cpu_threadtype_t slot_threadtype(int slot_num)
{
    return CPU_THREADTYPE_NONE;
}

/**
 * cpu_subtype
 *
 * Return the current cpu type for the current processor.
 */
cpu_subtype_t cpu_subtype(void)
{
    return current_cpu_datap()->cpu_subtype;
}

/**
 * cpu_threadtype
 *
 * Return the current SMT type for the current processor.
 */
cpu_threadtype_t cpu_threadtype(void)
{
    return CPU_THREADTYPE_NONE;
}

/**
 * ast_pending
 *
 * Returns the current pending Asynchronous System Trap.
 */
ast_t *ast_pending(void)
{
    return ((ast_t *)&current_cpu_datap()->cpu_pending_ast);
}

 /*ARGSUSED*/
kern_return_t cpu_control(int slot_num, processor_info_t info, unsigned int count)
{
    kprintf("cpu_control(%d,%p,%d) not implemented\n", slot_num, info, count);
    return (KERN_FAILURE);
}

kern_return_t cpu_info(processor_flavor_t flavor, int slot_num, processor_info_t info,
                       unsigned int *count)
{
    kprintf("cpu_info(%d,%d,%p,%p) not implemented\n", flavor, slot_num, info, count);
    return (KERN_FAILURE);
}

void cpu_machine_init(void)
{
    cpu_data_t *cdp = current_cpu_datap();

    PE_cpu_machine_init(cdp->cpu_id, FALSE);
    cdp->cpu_running = TRUE;
    ml_init_interrupt();
}

#if defined(BOARD_CONFIG_EXYNOS4210_I9100)
/*
 * SMP: CPU1 bring-up for Exynos4210 (QEMU smdkc210 + real GT-I9100).
 *
 * The secondary Cortex-A9 is parked in iROM/smpboot: it enables its own
 * GIC CPU interface, WFI-sleeps, then polls a boot-address mailbox
 * (PMU INFORM5 0x10020814 on rev1.1 / QEMU; SYSRAM slots on others)
 * and branches to the value found there.  We publish a position-
 * independent trampoline at a physical address; it replays TTBR0/
 * TTBCR/DACR/VBAR, sets ACTLR.SMP, turns on the MMU, and jumps into
 * i9100_cpu1_main() at its virtual address.
 *
 * Trampoline opcodes produced by tools/i9100_cpu1_tramp.S
 * (arm-none-eabi-as).  CPU1 enters at the PHYSICAL array address with
 * MMU off.  It mirrors TTBR0/TTBR1/TTBCR/DACR/VBAR, installs a 1MB
 * identity section for its own physical page in the *masked* TTBR1
 * base (register low bits are attributes, not base — that off-by-0x18
 * bug corrupted slot 0x40A and left 0x404 empty), sets ACTLR.SMP,
 * enables the MMU, then bx to the C entry VA.
 * Data slots live at words [76..85]:
 *   [0] TTBR0 [1] TTBR1 [2] TTBCR [3] DACR
 *   [4] SCTLR [5] VBAR  [6] SP    [7] ENTRY
 *   [8] alive_phys  [9] heartbeat_phys
 */
#define I9100_SMP_TD 76U
static volatile uint32_t i9100_smp_tramp[86] __attribute__((aligned(32))) = {
    0xf10c00c0, 0xe28f4f49, 0xe3a01001, 0xe5940020,
    0xe5801000, 0xf57ff04f, 0xe8941fe0, 0xe3a01002,
    0xe5801000, 0xe3a00000, 0xee080f17, 0xee070f15,
    0xee070fd5, 0xf57ff04f, 0xf57ff06f, 0xe5940020,
    0xe3a01003, 0xe5801000, 0xee025f10, 0xee026f30,
    0xee027f50, 0xee038f10, 0xee0caf10, 0xe5940020,
    0xe3a01004, 0xe5801000, 0xe1a00a2f, 0xe1a01726,
    0xe1a01701, 0xe0811100, 0xe5912000, 0xe3520000,
    0x1a000007, 0xe3002c0e, 0xe1822a00, 0xe5812000,
    0xe3a02000, 0xee082f17, 0xee072f15, 0xf57ff04f,
    0xf57ff06f, 0xee110f30, 0xe3800040, 0xe3800001,
    0xee010f30, 0xf57ff06f, 0xee110f10, 0xe1800009,
    0xee010f10, 0xf57ff04f, 0xf57ff06f, 0xe5940020,
    0xe3a01005, 0xe5801000, 0xe1a0d00b, 0xe10f0000,
    0xe3c0101f, 0xe3812012, 0xe121f002, 0xe1a0d00b,
    0xe3812017, 0xe121f002, 0xe1a0d00b, 0xe381201b,
    0xe121f002, 0xe1a0d00b, 0xe3812011, 0xe121f002,
    0xe1a0d00b, 0xe121f000, 0xe1a0d00b, 0xe5940020,
    0xe3a01006, 0xe5801000, 0xe12fff1c, 0xe1a00000,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static uint8_t i9100_cpu1_istack[8192] __attribute__((aligned(16)));
static processor_t i9100_proc1;
static volatile uint32_t i9100_cpu1_alive, i9100_cpu1_heartbeat;
static vm_offset_t i9100_smp_phys_delta = 0x40000000U; /* va-pa in kernel image */

extern void ios7j1_debug_putc(char);
extern int i9100_smp_map(void);
extern void i9100_smp_fire(uint32_t tramp_pa);

void i9100_cpu1_main(void)
{
    /* Proof-of-life milestone: CPU1 reaches kernel virtual context via
     * the TTBR1-mirroring trampoline (mailbox -> iROM -> phys entry ->
     * identity section -> MMU on -> kernel VA).  Full scheduler entry
     * (cpu_init/slave_main/processor_up) still trips on single-CPU
     * assumptions (commpage assert, per-CPU init) — parked for a later
     * milestone; i9100smp=0 leaves the whole path dormant anyway. */
    /* The flag write lands in our L1 only — CPU0's invalidate-then-read
     * sees stale DRAM (observed: report showed the last phys-written
     * stage marker).  Push the line out so CPU0 can see stage 7. */
    i9100_cpu1_alive = 7;   /* tramp stages write 1..6; 7 = C context */
    arm_dcache_wbinv_range((vaddr_t) &i9100_cpu1_alive, 64);
    for (;;) {
        i9100_cpu1_heartbeat++;
        if ((i9100_cpu1_heartbeat & 0xFFFF) == 0)
            arm_dcache_wbinv_range((vaddr_t) &i9100_cpu1_heartbeat, 64);
        __asm__ volatile("wfe" ::: "memory");
    }
}

static void i9100_cpu1_worker(void *p, wait_result_t w)
{
    (void) p; (void) w;
    for (;;) {
        volatile int i;
        __sync_add_and_fetch(&i9100_cpu1_heartbeat, 1);
        for (i = 0; i < 200000; i++)
            ;
    }
}

static void i9100_smp_puthex(uint32_t v)
{
    int q;
    for (q = 0; q < 8; q++) {
        unsigned int n = (v >> ((7 - q) * 4)) & 15U;
        ios7j1_debug_putc((char)(n < 10 ? ('0' + n) : ('a' + n - 10)));
    }
}

static kern_return_t i9100_smp_fire_and_wait(void)
{
    uint32_t dacr;
    int i;

    /* CPU0 runs TTBCR N=2: TTBR0 owns the low 1GB (user), TTBR1 owns
     * everything above (kernel VAs AND our phys-identity alias). CPU1
     * must mirror both or every kernel fetch faults into a recursive
     * prefetch abort (observed: CPU1 stuck at VBAR+0xc). */
    i9100_smp_tramp[I9100_SMP_TD + 0] = armreg_ttbr_read();
    __asm__ volatile("mrc p15,0,%0,c2,c0,1" : "=r"(dacr));
    i9100_smp_tramp[I9100_SMP_TD + 1] = dacr;   /* TTBR1 */
    i9100_smp_tramp[I9100_SMP_TD + 2] = armreg_ttbcr_read();
    __asm__ volatile("mrc p15,0,%0,c3,c0,0" : "=r"(dacr));
    i9100_smp_tramp[I9100_SMP_TD + 3] = dacr;   /* DACR */
    /* mirror CPU0's full SCTLR (M|C|Z|I plus the ARMv7 SBOP bits a
     * bare reset SCTLR lacks). */
    __asm__ volatile("mrc p15,0,%0,c1,c0,0" : "=r"(dacr));
    i9100_smp_tramp[I9100_SMP_TD + 4] = dacr;   /* SCTLR */
    i9100_smp_tramp[I9100_SMP_TD + 5] = armreg_vbar_read();
    i9100_smp_tramp[I9100_SMP_TD + 6] = i9100_cpu1_data.cpu_int_stack_top;
    i9100_smp_tramp[I9100_SMP_TD + 7] = (uint32_t) i9100_cpu1_main;
    i9100_smp_tramp[I9100_SMP_TD + 8] =
        (uint32_t) &i9100_cpu1_alive - i9100_smp_phys_delta;
    i9100_smp_tramp[I9100_SMP_TD + 9] =
        (uint32_t) &i9100_cpu1_heartbeat - i9100_smp_phys_delta;
    arm_dcache_wbinv_range((vaddr_t) &i9100_smp_tramp[0], sizeof(i9100_smp_tramp));
    arm_dcache_wbinv_range((vaddr_t) &i9100_cpu1_data, sizeof(i9100_cpu1_data));

    i9100_smp_fire((uint32_t) &i9100_smp_tramp[0] - i9100_smp_phys_delta);

    for (i = 0; i < 20000000; i++) {
        /* park path writes flags through DRAM while our L1 may hold
         * stale lines — invalidate before each check (harmless in the
         * coherent fast path).  Wait for stage 7 (C context) — any
         * lower stage just means QEMU hasn't scheduled the vCPU yet. */
        arm_dcache_inv_range((vaddr_t) &i9100_cpu1_alive, 64);
        if (i9100_cpu1_alive == 7)
            return KERN_SUCCESS;
    }
    return KERN_FAILURE;
}

/*
 * i9100_smp_init: create + start the second processor.  Called late in
 * boot (IOKit platform setup) so paging, the scheduler and the master
 * GIC are already live.  Returns nonzero if CPU1 was launched.
 */
int i9100_smp_init(void)
{
    ios7j1_debug_putc('S'); ios7j1_debug_putc('0');
    processor_t proc;
    thread_t hb;
    spl_t s;

    if (i9100_cpu1_alive)
        return 1;
    if (!i9100_smp_map()) {
        ios7j1_debug_putc('S'); ios7j1_debug_putc('M'); ios7j1_debug_putc('P');
        ios7j1_debug_putc('!'); ios7j1_debug_putc('M'); ios7j1_debug_putc('A');
        ios7j1_debug_putc('P'); ios7j1_debug_putc('\n');
        return 0;
    }
    ios7j1_debug_putc('A');
    /* CPU0 joins the coherent cluster (SCU already enabled by bootrom
     * on both QEMU and real hw; ACTLR.SMP makes L1 snoopy). */
    __asm__ volatile(
        "mrc p15,0,r0,c1,c0,1\n\t"
        "orr r0,r0,#0x40\n\t"
        "orr r0,r0,#0x1\n\t"
        "mcr p15,0,r0,c1,c0,1\n\t"
        ::: "r0", "memory");
    OSMemoryBarrier();
    ios7j1_debug_putc('B');

    proc = cpu_processor_alloc(FALSE);
    if (!proc)
        return 0;
    i9100_proc1 = proc;
    ios7j1_debug_putc('C');

    bzero(&i9100_cpu1_data, sizeof(i9100_cpu1_data));
    /* cpu_data_ptr[1] already points here (registered in cpu_bootstrap
     * while the page was still writable). */
    i9100_cpu1_data.cpu_this = &i9100_cpu1_data;
    i9100_cpu1_data.cpu_processor = proc;
    i9100_cpu1_data.cpu_number = 1;
    i9100_cpu1_data.cpu_phys_number = 1;
    i9100_cpu1_data.cpu_preemption_level = 1;
    i9100_cpu1_data.cpu_pending_ast = AST_NONE;
    i9100_cpu1_data.cpu_int_stack_top =
        (vm_offset_t)(i9100_cpu1_istack + sizeof(i9100_cpu1_istack));
    arm_dcache_wbinv_range((vaddr_t) &i9100_cpu1_data, sizeof(i9100_cpu1_data));
    ios7j1_debug_putc('D');

    processor_init(proc, 1, &pset0);
    /* give CPU1 a non-NULL active thread so its fleh/sleh dump path
     * (thread->kernel_stack reads) doesn't recursively abort. */
    i9100_cpu1_data.cpu_active_thread = proc->idle_thread;
    i9100_cpu1_data.cpu_active_stack = proc->idle_thread ?
        (vm_offset_t)proc->idle_thread->kernel_stack : 0;
    i9100_cpu1_data.cpu_kernel_stack = i9100_cpu1_data.cpu_active_stack;
    arm_dcache_wbinv_range((vaddr_t) &i9100_cpu1_data, sizeof(i9100_cpu1_data));
    ios7j1_debug_putc('E');

    /* Milestone 1: wake CPU1 into kernel context only.  processor_start /
     * slave_main wiring into the Mach scheduler is deferred — the commpage
     * active-cpus assert and per-CPU init paths still assume one CPU. */
    if (i9100_smp_fire_and_wait() != KERN_SUCCESS) {
        ios7j1_debug_putc('!'); ios7j1_debug_putc('S');
        return 0;
    }
    ios7j1_debug_putc('F');
    (void) hb; (void) s;
    ios7j1_debug_putc('G');
    return 1;
}

/* CPU0-visible proof of life. */
void i9100_smp_report(void)
{
    uint32_t a, h0;
    int i;
    arm_dcache_inv_range((vaddr_t) &i9100_cpu1_alive, 64);
    a = i9100_cpu1_alive;
    h0 = i9100_cpu1_heartbeat;
    for (i = 0; i < 400000; i++)
        ;
    /* re-sample after the delay — the first read can catch CPU1 still
     * mid-trampoline (observed: report showed 3 while CPU1 was already
     * spinning in i9100_cpu1_main). */
    arm_dcache_inv_range((vaddr_t) &i9100_cpu1_alive, 64);
    a = i9100_cpu1_alive;
    ios7j1_debug_putc('S'); ios7j1_debug_putc('M'); ios7j1_debug_putc('P');
    ios7j1_debug_putc(' ');
    i9100_smp_puthex(a);
    ios7j1_debug_putc(' ');
    i9100_smp_puthex(h0);
    ios7j1_debug_putc(' ');
    i9100_smp_puthex(i9100_cpu1_heartbeat);
    ios7j1_debug_putc('\n');
}
#endif /* BOARD_CONFIG_EXYNOS4210_I9100 */

kern_return_t cpu_start(int cpu)
{
    if (cpu == cpu_number()) {
        cpu_machine_init();
        return KERN_SUCCESS;
    }

#if defined(BOARD_CONFIG_EXYNOS4210_I9100)
    if (cpu == 1)
        return i9100_smp_fire_and_wait();
#endif
    return KERN_SUCCESS;
}

kern_return_t cpu_info_count(processor_flavor_t flavor, unsigned int *count)
{
    *count = 0;
    return KERN_FAILURE;
}
